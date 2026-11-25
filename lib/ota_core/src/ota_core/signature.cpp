#include "ota_core/signature.h"

#include <cstring>

#include "ota_core/keys.h"
#include "ota_core/sha256.h"

namespace ota_core {

namespace {

constexpr char kMagic[] = "PLANERADAR-OTA-v1";  // plus its NUL: 18 bytes in the payload
constexpr size_t kMagicBytes = sizeof(kMagic);
constexpr char kDevIdPrefix[] = "PLANERADAR-DEVID-v1";
constexpr size_t kDevIdPrefixBytes = sizeof(kDevIdPrefix);
constexpr size_t kSigB64Chars = 88;
constexpr size_t kPublishedHashBytes = 16;

static_assert(kMagicBytes == 18, "payload magic must be 18 bytes with its NUL");
static_assert(kMagicBytes + kShaBytes + 4 + 4 + 1 + kDeviceIdBytes == kPayloadBytes, "payload size");

int b64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;  // uppercase is refused: the hash has one spelling
}

bool decodeShaHex(const char* hex, uint8_t out[kShaBytes]) {
  if (hex == nullptr || strlen(hex) != kShaBytes * 2) return false;
  for (size_t i = 0; i < kShaBytes; ++i) {
    const int hi = hexValue(hex[2 * i]);
    const int lo = hexValue(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

void putBe32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v >> 24);
  p[1] = static_cast<uint8_t>(v >> 16);
  p[2] = static_cast<uint8_t>(v >> 8);
  p[3] = static_cast<uint8_t>(v);
}

bool allZero(const uint8_t* p, size_t n) {
  uint8_t acc = 0;
  for (size_t i = 0; i < n; ++i) acc |= p[i];
  return acc == 0;
}

}  // namespace

const char* sigErrorName(SigError e) {
  switch (e) {
    case SigError::kNone: return "none";
    case SigError::kMissingSignature: return "missing_signature";
    case SigError::kBadSignatureEncoding: return "bad_signature_encoding";
    case SigError::kUnknownChannel: return "unknown_channel";
    case SigError::kChannelNotAccepted: return "channel_not_accepted";
    case SigError::kBadHash: return "bad_hash";
    case SigError::kBadBuild: return "bad_build";
    case SigError::kBadMinBuild: return "bad_min_build";
    case SigError::kMinBuildAboveBuild: return "min_build_above_build";
    case SigError::kBadDeviceId: return "bad_device_id";
    case SigError::kBadSignature: return "bad_signature";
  }
  return "unknown";
}

bool decodeSignatureB64(const char* b64, uint8_t out[kSigBytes]) {
  if (b64 == nullptr || strlen(b64) != kSigB64Chars) return false;
  if (b64[86] != '=' || b64[87] != '=') return false;
  uint8_t tmp[kSigBytes];
  size_t n = 0;
  for (size_t i = 0; i < 84; i += 4) {
    int v[4];
    for (int k = 0; k < 4; ++k) {
      v[k] = b64Value(b64[i + k]);
      if (v[k] < 0) return false;
    }
    tmp[n++] = static_cast<uint8_t>((v[0] << 2) | (v[1] >> 4));
    tmp[n++] = static_cast<uint8_t>(((v[1] & 0x0F) << 4) | (v[2] >> 2));
    tmp[n++] = static_cast<uint8_t>(((v[2] & 0x03) << 6) | v[3]);
  }
  const int a = b64Value(b64[84]);
  const int b = b64Value(b64[85]);
  if (a < 0 || b < 0 || (b & 0x0F) != 0) return false;  // non-canonical trailing bits
  tmp[n++] = static_cast<uint8_t>((a << 2) | (b >> 4));
  memcpy(out, tmp, kSigBytes);
  return true;
}

void computeDeviceId(const uint8_t mac6[6], uint8_t out[kDeviceIdBytes]) {
  Sha256 h;
  h.start();
  h.update(reinterpret_cast<const uint8_t*>(kDevIdPrefix), kDevIdPrefixBytes);
  h.update(mac6, 6);
  h.finish(out);
}

void deviceHashHex(const uint8_t id[kDeviceIdBytes], char out[33]) {
  static const char kDigits[] = "0123456789abcdef";
  for (size_t i = 0; i < 16; ++i) {
    out[2 * i] = kDigits[id[i] >> 4];
    out[2 * i + 1] = kDigits[id[i] & 0x0F];
  }
  out[32] = '\0';
}

size_t buildPayload(const uint8_t sha[kShaBytes], uint32_t build, uint32_t minBuild, Channel channel,
                    const uint8_t deviceId[kDeviceIdBytes], uint8_t out[kPayloadBytes]) {
  memcpy(out, kMagic, kMagicBytes);
  memcpy(out + 18, sha, kShaBytes);
  putBe32(out + 50, build);
  putBe32(out + 54, minBuild);
  out[58] = static_cast<uint8_t>(channel);
  // The id field is 32 bytes, but only the 16-byte hash a board publishes can be signed by
  // someone who has not read the board's full id: rc carries that hash, zero-padded.
  memset(out + 59, 0, kDeviceIdBytes);
  if (channel == Channel::kRc) memcpy(out + 59, deviceId, kPublishedHashBytes);
  return kPayloadBytes;
}

SigError verifyManifest(const ManifestFields& f, uint8_t acceptedChannelMask,
                        const uint8_t ownDeviceId[kDeviceIdBytes], const KeyList& keys,
                        Ed25519VerifyFn verify, VerifiedOffer& out) {
  if (f.signature == nullptr) return SigError::kMissingSignature;
  uint8_t sig[kSigBytes];
  if (!decodeSignatureB64(f.signature, sig)) return SigError::kBadSignatureEncoding;

  Channel channel;
  if (f.channel != nullptr && strcmp(f.channel, "prod") == 0) {
    channel = Channel::kProd;
  } else if (f.channel != nullptr && strcmp(f.channel, "rc") == 0) {
    channel = Channel::kRc;
  } else {
    return SigError::kUnknownChannel;
  }
  if ((acceptedChannelMask & channelBit(channel)) == 0) return SigError::kChannelNotAccepted;

  uint8_t sha[kShaBytes];
  if (!decodeShaHex(f.sha256Hex, sha)) return SigError::kBadHash;
  if (!f.build.valid) return SigError::kBadBuild;
  if (!f.minBuild.valid) return SigError::kBadMinBuild;
  if (f.minBuild.value > f.build.value) return SigError::kMinBuildAboveBuild;
  if (allZero(ownDeviceId, kDeviceIdBytes)) return SigError::kBadDeviceId;

  uint8_t payload[kPayloadBytes];
  buildPayload(sha, f.build.value, f.minBuild.value, channel, ownDeviceId, payload);
  bool ok = false;
  for (size_t i = 0; i < keys.count && !ok; ++i) {
    if (keys::isDisabled(keys.keys[i])) continue;
    ok = verify(sig, keys.keys[i], payload, kPayloadBytes);
  }
  if (!ok) return SigError::kBadSignature;

  memcpy(out.sha256, sha, kShaBytes);
  out.build = f.build.value;
  out.minBuild = f.minBuild.value;
  out.channel = channel;
  return SigError::kNone;
}

}  // namespace ota_core

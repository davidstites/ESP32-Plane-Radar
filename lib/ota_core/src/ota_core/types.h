#pragma once

#include <cstddef>
#include <cstdint>

namespace ota_core {

constexpr size_t kKeyBytes = 32;
constexpr size_t kSigBytes = 64;
constexpr size_t kShaBytes = 32;
constexpr size_t kDeviceIdBytes = 32;
constexpr size_t kPayloadBytes = 91;

/** Which manifest stream an offer belongs to. The value is signed into the payload. */
enum class Channel : uint8_t { kProd = 1, kRc = 2 };

/** Bit for use in an accepted-channel mask. */
constexpr uint8_t channelBit(Channel c) { return static_cast<uint8_t>(1u << static_cast<uint8_t>(c)); }

/** Ed25519 verify: true when `sig` is a valid signature of `msg` under `pub`. */
using Ed25519VerifyFn = bool (*)(const uint8_t* sig, const uint8_t* pub, const uint8_t* msg, size_t len);

/** The firmware's trusted public keys. A key of all zeros is a disabled slot. */
struct KeyList {
  const uint8_t* const* keys;
  size_t count;
};

/** A JSON number that may be absent or out of range. */
struct U32Field {
  bool valid;
  uint32_t value;
};

/** Raw manifest fields as parsed; nothing here is trusted until verifyManifest accepts it. */
struct ManifestFields {
  U32Field build;
  U32Field minBuild;
  const char* sha256Hex;
  const char* channel;
  const char* signature;
};

/** The signed facts of a manifest that passed verification. */
struct VerifiedOffer {
  uint8_t sha256[kShaBytes];
  uint32_t build;
  uint32_t minBuild;
  Channel channel;
};

}  // namespace ota_core

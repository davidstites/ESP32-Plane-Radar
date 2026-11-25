#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "ota_core/keys.h"
#include "ota_core/signature.h"
#include "ota_core/types.h"
#include "support/ota_vectors.h"
#include "support/tweetnacl_verify.h"

using namespace ota_core;
namespace v = ota_vectors;

namespace {

const uint8_t kZeroKey[kKeyBytes] = {0};
const uint8_t kZeroId[kDeviceIdBytes] = {0};
const uint8_t* const kOneKey[] = {v::kPub};
constexpr uint8_t kBothChannels = channelBit(Channel::kProd) | channelBit(Channel::kRc);

ManifestFields goodProd() {
  ManifestFields f{};
  f.build = {true, v::kBuild};
  f.minBuild = {true, v::kMinBuild};
  f.sha256Hex = v::kShaHex;
  f.channel = "prod";
  f.signature = v::kSigProd;
  return f;
}

ManifestFields goodRc() {
  ManifestFields f = goodProd();
  f.channel = "rc";
  f.signature = v::kSigRc;
  return f;
}

SigError verify(const ManifestFields& f, uint8_t mask = kBothChannels,
                const uint8_t* id = v::kDeviceId, KeyList keys = KeyList{kOneKey, 1}) {
  VerifiedOffer out{};
  return verifyManifest(f, mask, id, keys, tweetnaclEd25519Verify, out);
}

}  // namespace

TEST(Payload, LayoutIsExact) {
  uint8_t out[kPayloadBytes];
  ASSERT_EQ(buildPayload(v::kSha, v::kBuild, v::kMinBuild, Channel::kProd, kZeroId, out), kPayloadBytes);
  EXPECT_EQ(0, memcmp(out, v::kPayloadProd, kPayloadBytes));
  EXPECT_EQ(0, memcmp(out, "PLANERADAR-OTA-v1\0", 18));
  EXPECT_EQ(0, memcmp(out + 18, v::kSha, 32));
  const uint8_t build_be[] = {0, 0, 0, 8};
  const uint8_t min_be[] = {0, 0, 0, 5};
  EXPECT_EQ(0, memcmp(out + 50, build_be, 4));
  EXPECT_EQ(0, memcmp(out + 54, min_be, 4));
  EXPECT_EQ(out[58], 0x01);
  EXPECT_EQ(0, memcmp(out + 59, kZeroId, 32));

  ASSERT_EQ(buildPayload(v::kSha, v::kBuild, v::kMinBuild, Channel::kRc, v::kDeviceId, out), kPayloadBytes);
  EXPECT_EQ(0, memcmp(out, v::kPayloadRc, kPayloadBytes));
  EXPECT_EQ(out[58], 0x02);
  EXPECT_EQ(0, memcmp(out + 59, v::kDeviceId, 16));  // the published hash...
  EXPECT_EQ(0, memcmp(out + 75, kZeroId, 16));       // ...padded with zeros
}

TEST(Payload, ProdIgnoresTheDeviceIdArgument) {
  uint8_t out[kPayloadBytes];
  buildPayload(v::kSha, v::kBuild, v::kMinBuild, Channel::kProd, v::kDeviceId, out);
  EXPECT_EQ(0, memcmp(out, v::kPayloadProd, kPayloadBytes));
}

TEST(Payload, ChannelBitsAreDistinct) {
  EXPECT_EQ(channelBit(Channel::kProd), 2);
  EXPECT_EQ(channelBit(Channel::kRc), 4);
}

TEST(Verify, GoldenProdSignatureVerifies) {
  VerifiedOffer out{};
  ASSERT_EQ(verifyManifest(goodProd(), kBothChannels, v::kDeviceId, KeyList{kOneKey, 1},
                           tweetnaclEd25519Verify, out),
            SigError::kNone);
  EXPECT_EQ(out.build, v::kBuild);
  EXPECT_EQ(out.minBuild, v::kMinBuild);
  EXPECT_EQ(out.channel, Channel::kProd);
  EXPECT_EQ(0, memcmp(out.sha256, v::kSha, 32));
}

TEST(Verify, GoldenRcSignatureVerifies) {
  VerifiedOffer out{};
  ASSERT_EQ(verifyManifest(goodRc(), kBothChannels, v::kDeviceId, KeyList{kOneKey, 1},
                           tweetnaclEd25519Verify, out),
            SigError::kNone);
  EXPECT_EQ(out.channel, Channel::kRc);
}

TEST(Verify, TamperedBuildFailsBadSignature) {
  ManifestFields f = goodProd();
  f.build.value = v::kBuild + 1;
  EXPECT_EQ(verify(f), SigError::kBadSignature);
}

TEST(Verify, WrongKeyFailsBadSignature) {
  ManifestFields f = goodProd();
  f.signature = v::kSigWrongKey;
  EXPECT_EQ(verify(f), SigError::kBadSignature);
}

TEST(Verify, ProdSignatureDoesNotVerifyAsRc) {
  ManifestFields f = goodProd();
  f.channel = "rc";
  EXPECT_EQ(verify(f), SigError::kBadSignature);
}

TEST(Verify, RcSignatureDoesNotVerifyAsProd) {
  ManifestFields f = goodRc();
  f.channel = "prod";
  EXPECT_EQ(verify(f), SigError::kBadSignature);
}

TEST(Verify, RcSignedForAnotherBoardFails) {
  uint8_t other[kDeviceIdBytes];
  memcpy(other, v::kDeviceId, sizeof(other));
  other[0] ^= 0x01;
  EXPECT_EQ(verify(goodRc(), kBothChannels, other), SigError::kBadSignature);
}

TEST(Verify, AllZeroOwnDeviceIdIsBadDeviceId) {
  EXPECT_EQ(verify(goodProd(), kBothChannels, kZeroId), SigError::kBadDeviceId);
}

TEST(Verify, DisabledKeySkippedAndSecondKeyAccepted) {
  const uint8_t* keys[] = {kZeroKey, v::kPub};
  EXPECT_EQ(verify(goodProd(), kBothChannels, v::kDeviceId, KeyList{keys, 2}), SigError::kNone);
}

TEST(Verify, AllKeysDisabledIsBadSignature) {
  const uint8_t* keys[] = {kZeroKey, kZeroKey};
  EXPECT_EQ(verify(goodProd(), kBothChannels, v::kDeviceId, KeyList{keys, 2}), SigError::kBadSignature);
}

TEST(Verify, MissingSignature) {
  ManifestFields f = goodProd();
  f.signature = nullptr;
  EXPECT_EQ(verify(f), SigError::kMissingSignature);
}

TEST(Verify, BadSignatureEncoding) {
  ManifestFields f = goodProd();
  f.signature = "short";
  EXPECT_EQ(verify(f), SigError::kBadSignatureEncoding);
}

TEST(Verify, UnknownChannel) {
  ManifestFields f = goodProd();
  f.channel = "dev";
  EXPECT_EQ(verify(f), SigError::kUnknownChannel);
  f.channel = nullptr;
  EXPECT_EQ(verify(f), SigError::kUnknownChannel);
}

TEST(Verify, ChannelNotAccepted) {
  EXPECT_EQ(verify(goodRc(), channelBit(Channel::kProd)), SigError::kChannelNotAccepted);
  EXPECT_EQ(verify(goodProd(), channelBit(Channel::kRc)), SigError::kChannelNotAccepted);
}

TEST(Verify, BadHash) {
  ManifestFields f = goodProd();
  f.sha256Hex = "zz";
  EXPECT_EQ(verify(f), SigError::kBadHash);
  f.sha256Hex = nullptr;
  EXPECT_EQ(verify(f), SigError::kBadHash);
  std::string upper = v::kShaHex;
  for (char& c : upper) c = static_cast<char>(toupper(c));
  f.sha256Hex = upper.c_str();
  EXPECT_EQ(verify(f), SigError::kBadHash);
  std::string nonhex = v::kShaHex;
  nonhex[10] = 'g';
  f.sha256Hex = nonhex.c_str();
  EXPECT_EQ(verify(f), SigError::kBadHash);
}

TEST(Verify, BadBuild) {
  ManifestFields f = goodProd();
  f.build.valid = false;
  EXPECT_EQ(verify(f), SigError::kBadBuild);
}

TEST(Verify, BadMinBuild) {
  ManifestFields f = goodProd();
  f.minBuild.valid = false;
  EXPECT_EQ(verify(f), SigError::kBadMinBuild);
}

TEST(Verify, MinBuildAboveBuild) {
  ManifestFields f = goodProd();
  f.minBuild.value = v::kBuild + 1;
  EXPECT_EQ(verify(f), SigError::kMinBuildAboveBuild);
}

TEST(Verify, ChecksRunInEnumOrder) {
  ManifestFields f = goodProd();
  f.signature = nullptr;
  f.channel = "dev";
  f.build.valid = false;
  EXPECT_EQ(verify(f), SigError::kMissingSignature);
  f.signature = v::kSigProd;
  EXPECT_EQ(verify(f), SigError::kUnknownChannel);
  f.channel = "prod";
  EXPECT_EQ(verify(f), SigError::kBadBuild);
}

TEST(Verify, OutUntouchedOnError) {
  VerifiedOffer out{};
  out.build = 4242;
  out.minBuild = 17;
  out.channel = Channel::kRc;
  memset(out.sha256, 0xAB, sizeof(out.sha256));
  ManifestFields f = goodProd();
  f.build.value = v::kBuild + 1;  // breaks the signature, the last check
  ASSERT_EQ(verifyManifest(f, kBothChannels, v::kDeviceId, KeyList{kOneKey, 1}, tweetnaclEd25519Verify, out),
            SigError::kBadSignature);
  EXPECT_EQ(out.build, 4242u);
  EXPECT_EQ(out.minBuild, 17u);
  EXPECT_EQ(out.channel, Channel::kRc);
  EXPECT_EQ(out.sha256[0], 0xAB);
}

TEST(SigErrorName, IsSnakeCaseForEveryValue) {
  EXPECT_STREQ(sigErrorName(SigError::kNone), "none");
  EXPECT_STREQ(sigErrorName(SigError::kMissingSignature), "missing_signature");
  EXPECT_STREQ(sigErrorName(SigError::kBadSignatureEncoding), "bad_signature_encoding");
  EXPECT_STREQ(sigErrorName(SigError::kUnknownChannel), "unknown_channel");
  EXPECT_STREQ(sigErrorName(SigError::kChannelNotAccepted), "channel_not_accepted");
  EXPECT_STREQ(sigErrorName(SigError::kBadHash), "bad_hash");
  EXPECT_STREQ(sigErrorName(SigError::kBadBuild), "bad_build");
  EXPECT_STREQ(sigErrorName(SigError::kBadMinBuild), "bad_min_build");
  EXPECT_STREQ(sigErrorName(SigError::kMinBuildAboveBuild), "min_build_above_build");
  EXPECT_STREQ(sigErrorName(SigError::kBadDeviceId), "bad_device_id");
  EXPECT_STREQ(sigErrorName(SigError::kBadSignature), "bad_signature");
  EXPECT_STREQ(sigErrorName(static_cast<SigError>(200)), "unknown");
}

TEST(DecodeSignature, AcceptsTheGoldenSignature) {
  uint8_t sig[kSigBytes];
  ASSERT_TRUE(decodeSignatureB64(v::kSigProd, sig));
  uint8_t sig2[kSigBytes];
  ASSERT_TRUE(decodeSignatureB64(v::kSigRc, sig2));
  EXPECT_NE(0, memcmp(sig, sig2, kSigBytes));
}

TEST(DecodeSignature, RejectsWrongLengthPaddingNonCanonicalAlphabetAndNull) {
  uint8_t sig[kSigBytes];
  std::string good = v::kSigProd;
  ASSERT_EQ(good.size(), 88u);
  EXPECT_FALSE(decodeSignatureB64(nullptr, sig));
  EXPECT_FALSE(decodeSignatureB64(good.substr(0, 87).c_str(), sig));
  EXPECT_FALSE(decodeSignatureB64((good + "A").c_str(), sig));
  std::string one_pad = good;
  one_pad[86] = 'A';
  EXPECT_FALSE(decodeSignatureB64(one_pad.c_str(), sig));
  std::string no_pad = good;
  no_pad[87] = 'A';
  EXPECT_FALSE(decodeSignatureB64(no_pad.c_str(), sig));
  std::string noncanonical = good;
  noncanonical[85] = 'B';  // low 4 bits of the final data character must be zero
  EXPECT_FALSE(decodeSignatureB64(noncanonical.c_str(), sig));
  std::string bad_char = good;
  bad_char[3] = '!';
  EXPECT_FALSE(decodeSignatureB64(bad_char.c_str(), sig));
  std::string urlsafe = good;
  urlsafe[3] = '-';
  EXPECT_FALSE(decodeSignatureB64(urlsafe.c_str(), sig));
}

TEST(DeviceId, IsSha256OfPrefixedMac) {
  uint8_t id[kDeviceIdBytes];
  computeDeviceId(v::kMac, id);
  EXPECT_EQ(0, memcmp(id, v::kDeviceId, kDeviceIdBytes));
}

TEST(DeviceId, HashHexIsFirst16BytesLowercase) {
  char hex[33];
  memset(hex, 'X', sizeof(hex));
  deviceHashHex(v::kDeviceId, hex);
  std::string want;
  char b[3];
  for (int i = 0; i < 16; ++i) {
    snprintf(b, sizeof(b), "%02x", v::kDeviceId[i]);
    want += b;
  }
  EXPECT_EQ(std::string(hex), want);
  EXPECT_EQ(hex[32], '\0');
}

TEST(Keys, ListHasThreeSlotsAndDisabledMeansAllZero) {
  const KeyList list = keys::keyList();
  EXPECT_EQ(list.count, 3u);
  EXPECT_TRUE(keys::isDisabled(kZeroKey));
  EXPECT_FALSE(keys::isDisabled(v::kPub));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

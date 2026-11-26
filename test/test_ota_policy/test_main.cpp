#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "ota_core/policy.h"

using namespace ota_core;

namespace {

LocalState local(uint32_t build = 5, uint32_t floor = 0) {
  return LocalState{build, floor, false, true};
}

const char kHash[] = "0123456789abcdef0123456789abcdef";

}  // namespace

TEST(Evaluate, AcceptsANewerOffer) {
  EXPECT_EQ(evaluateOffer(Offer{8, 5}, local(5)), Verdict::kAccept);
}

TEST(Evaluate, SuspiciousBuildIsRefused) {
  EXPECT_EQ(evaluateOffer(Offer{kMaxSaneBuild + 1, 0}, local()), Verdict::kSuspiciousBuild);
  EXPECT_EQ(evaluateOffer(Offer{kMaxSaneBuild, 0}, local()), Verdict::kAccept);
}

TEST(Evaluate, BlacklistedBuildIsRefused) {
  LocalState s = local();
  s.blacklisted = true;
  EXPECT_EQ(evaluateOffer(Offer{8, 0}, s), Verdict::kBlacklisted);
}

TEST(Evaluate, OfferBelowTheStoredFloorIsRefused) {
  EXPECT_EQ(evaluateOffer(Offer{6, 0}, local(5, 7)), Verdict::kBelowMinBuild);
  EXPECT_EQ(evaluateOffer(Offer{7, 0}, local(5, 7)), Verdict::kAccept);
}

TEST(Evaluate, OfferMinBuildRaisesTheEffectiveFloor) {
  EXPECT_EQ(evaluateOffer(Offer{6, 7}, local(5, 0)), Verdict::kBelowMinBuild);
}

TEST(Evaluate, NeitherFloorLowersTheOther) {
  EXPECT_EQ(evaluateOffer(Offer{8, 9}, local(5, 3)), Verdict::kBelowMinBuild);  // offer floor higher
  EXPECT_EQ(evaluateOffer(Offer{8, 3}, local(5, 9)), Verdict::kBelowMinBuild);  // stored floor higher
}

TEST(Evaluate, ServerRollbackIsRefused) {
  EXPECT_EQ(evaluateOffer(Offer{4, 0}, local(5, 0)), Verdict::kServerRollback);
}

TEST(Evaluate, SameBuildIsAlreadyCurrent) {
  EXPECT_EQ(evaluateOffer(Offer{5, 0}, local(5)), Verdict::kAlreadyCurrent);
}

TEST(Evaluate, OutsideTheRolloutIsNotInRollout) {
  LocalState s = local();
  s.inRollout = false;
  EXPECT_EQ(evaluateOffer(Offer{8, 0}, s), Verdict::kNotInRollout);
}

TEST(Evaluate, SuspiciousBeatsBlacklisted) {
  LocalState s = local();
  s.blacklisted = true;
  EXPECT_EQ(evaluateOffer(Offer{kMaxSaneBuild + 1, 0}, s), Verdict::kSuspiciousBuild);
}

TEST(Evaluate, BlacklistedBeatsBelowMinBuild) {
  LocalState s = local(5, 9);
  s.blacklisted = true;
  EXPECT_EQ(evaluateOffer(Offer{6, 0}, s), Verdict::kBlacklisted);
}

TEST(Evaluate, BelowMinBuildBeatsServerRollback) {
  EXPECT_EQ(evaluateOffer(Offer{3, 0}, local(5, 4)), Verdict::kBelowMinBuild);
}

TEST(Evaluate, AlreadyCurrentBeatsNotInRollout) {
  LocalState s = local(5);
  s.inRollout = false;
  EXPECT_EQ(evaluateOffer(Offer{5, 0}, s), Verdict::kAlreadyCurrent);
}

TEST(VerdictName, IsSnakeCaseForEveryValue) {
  EXPECT_STREQ(verdictName(Verdict::kAccept), "accept");
  EXPECT_STREQ(verdictName(Verdict::kSuspiciousBuild), "suspicious_build");
  EXPECT_STREQ(verdictName(Verdict::kBlacklisted), "blacklisted");
  EXPECT_STREQ(verdictName(Verdict::kBelowMinBuild), "below_min_build");
  EXPECT_STREQ(verdictName(Verdict::kServerRollback), "server_rollback");
  EXPECT_STREQ(verdictName(Verdict::kAlreadyCurrent), "already_current");
  EXPECT_STREQ(verdictName(Verdict::kNotInRollout), "not_in_rollout");
  EXPECT_STREQ(verdictName(static_cast<Verdict>(200)), "unknown");
}

TEST(Rollout, ZeroAndHundred) {
  uint8_t id[32] = {0};
  EXPECT_FALSE(inRolloutGroup(id, 0));
  EXPECT_TRUE(inRolloutGroup(id, 100));
  EXPECT_TRUE(inRolloutGroup(id, 255));
  id[0] = 0xFF;
  id[1] = 0xFF;
  EXPECT_FALSE(inRolloutGroup(id, 0));
  EXPECT_TRUE(inRolloutGroup(id, 100));
}

TEST(Rollout, BucketUsesTheFirstTwoBytesBigEndian) {
  uint8_t id[32] = {0};
  id[0] = 0x01;
  id[1] = 0x2C;  // 0x012C = 300 -> bucket 0
  EXPECT_TRUE(inRolloutGroup(id, 1));
  id[1] = 0x2D;  // 301 -> bucket 1
  EXPECT_FALSE(inRolloutGroup(id, 1));
  EXPECT_TRUE(inRolloutGroup(id, 2));
  id[5] = 0xAA;  // later bytes do not matter
  EXPECT_TRUE(inRolloutGroup(id, 2));
}

TEST(Rollout, BucketIsStableAndSpreads) {
  uint32_t x = 12345;
  int in = 0;
  for (int i = 0; i < 1000; ++i) {
    x = x * 1664525u + 1013904223u;
    uint8_t id[32] = {0};
    id[0] = static_cast<uint8_t>(x >> 24);
    id[1] = static_cast<uint8_t>(x >> 16);
    const bool first = inRolloutGroup(id, 50);
    EXPECT_EQ(first, inRolloutGroup(id, 50));
    if (first) ++in;
  }
  EXPECT_GT(in, 400);
  EXPECT_LT(in, 600);
}

TEST(Path, AcceptsNormalPaths) {
  EXPECT_TRUE(isAllowedPath("/planeradar/firmware-8.bin"));
  EXPECT_TRUE(isAllowedPath("/a"));
  EXPECT_TRUE(isAllowedPath("/a..b/c"));
  EXPECT_TRUE(isAllowedPath(("/" + std::string(95, 'a')).c_str()));  // 96 chars, the limit
}

TEST(Path, Rejects) {
  EXPECT_FALSE(isAllowedPath(nullptr));
  EXPECT_FALSE(isAllowedPath("firmware.bin"));
  EXPECT_FALSE(isAllowedPath("//evil/x"));
  EXPECT_FALSE(isAllowedPath("/a//b"));
  EXPECT_FALSE(isAllowedPath("/a/../b"));
  EXPECT_FALSE(isAllowedPath("/.."));
  EXPECT_FALSE(isAllowedPath("/a/.."));
  EXPECT_FALSE(isAllowedPath("/a?x=1"));
  EXPECT_FALSE(isAllowedPath("/a#b"));
  EXPECT_FALSE(isAllowedPath("https://h/x"));
  EXPECT_FALSE(isAllowedPath("/a b"));
  EXPECT_FALSE(isAllowedPath("/"));
  EXPECT_FALSE(isAllowedPath(""));
  EXPECT_FALSE(isAllowedPath(("/" + std::string(96, 'a')).c_str()));  // 97 chars
  EXPECT_FALSE(isAllowedPath("/\xff"));
}

TEST(RcPath, FormatsBaseRcHashJson) {
  char out[128];
  ASSERT_TRUE(formatRcPath("/planeradar", kHash, out, sizeof(out)));
  EXPECT_STREQ(out, "/planeradar/rc/0123456789abcdef0123456789abcdef.json");
}

TEST(RcPath, RejectsBadHashAndSmallBuffer) {
  char out[128];
  EXPECT_FALSE(formatRcPath("/p", "0123456789ABCDEF0123456789ABCDEF", out, sizeof(out)));
  EXPECT_FALSE(formatRcPath("/p", "0123456789abcdef0123456789abcde", out, sizeof(out)));
  EXPECT_FALSE(formatRcPath("/p", "0123456789abcdef0123456789abcdef0", out, sizeof(out)));
  EXPECT_FALSE(formatRcPath("/p", "0123456789abcdef0123456789abcdeg", out, sizeof(out)));
  EXPECT_FALSE(formatRcPath("/p", nullptr, out, sizeof(out)));
  char tiny[10];
  EXPECT_FALSE(formatRcPath("/planeradar", kHash, tiny, sizeof(tiny)));
  char exact[64];
  const size_t need = strlen("/p/rc/") + 32 + strlen(".json") + 1;
  ASSERT_LE(need, sizeof(exact));
  EXPECT_TRUE(formatRcPath("/p", kHash, exact, need));
  EXPECT_FALSE(formatRcPath("/p", kHash, exact, need - 1));
}

TEST(IsListed, ExactMatchOnly) {
  const char* entries[] = {"0123456789abcdef0123456789abcdef", "ffffffffffffffffffffffffffffffff"};
  EXPECT_TRUE(isListed(entries, 2, kHash));
  EXPECT_TRUE(isListed(entries, 2, "ffffffffffffffffffffffffffffffff"));
  EXPECT_FALSE(isListed(entries, 2, "0123456789abcdef0123456789abcdee"));
  EXPECT_FALSE(isListed(entries, 1, "ffffffffffffffffffffffffffffffff"));
  EXPECT_FALSE(isListed(entries, 0, kHash));
  EXPECT_FALSE(isListed(nullptr, 0, kHash));
}

TEST(ChooseInstall, TruthTable) {
  EXPECT_EQ(chooseInstall(false, 0, false, 0), InstallChoice::kNone);
  EXPECT_EQ(chooseInstall(true, 8, false, 0), InstallChoice::kProduction);
  EXPECT_EQ(chooseInstall(false, 0, true, 9), InstallChoice::kCandidate);
  EXPECT_EQ(chooseInstall(true, 8, true, 9), InstallChoice::kCandidate);
}

TEST(ChooseInstall, RcOlderThanProdDoesNotDowngrade) {
  EXPECT_EQ(chooseInstall(true, 9, true, 8), InstallChoice::kProduction);
}

TEST(ChooseInstall, PromotedRcBuildEqualsProdIsNotInstalled) {
  EXPECT_EQ(chooseInstall(true, 9, true, 9), InstallChoice::kProduction);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

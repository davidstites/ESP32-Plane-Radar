#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "ota_core/state.h"
#include "support/ota_fakes.h"

using namespace ota_core;
using ota_fakes::FakeStore;

TEST(State, FloorOnlyRises) {
  FakeStore store;
  OtaState s(store);
  EXPECT_EQ(s.floor(), 0u);
  EXPECT_TRUE(s.raiseFloor(5));
  EXPECT_EQ(s.floor(), 5u);
  const int writes = store.writeCount;
  EXPECT_TRUE(s.raiseFloor(3));  // already at or above: holds, writes nothing
  EXPECT_TRUE(s.raiseFloor(5));
  EXPECT_EQ(s.floor(), 5u);
  EXPECT_EQ(store.writeCount, writes);
  EXPECT_TRUE(s.raiseFloor(9));
  EXPECT_EQ(s.floor(), 9u);
}

TEST(State, PendingSetClear) {
  FakeStore store;
  OtaState s(store);
  EXPECT_EQ(s.pending(), 0u);
  EXPECT_TRUE(s.setPending(8));
  EXPECT_EQ(s.pending(), 8u);
  EXPECT_TRUE(s.clearPending());
  EXPECT_EQ(s.pending(), 0u);
}

TEST(State, BlacklistRecordsAndQueries) {
  FakeStore store;
  OtaState s(store);
  EXPECT_FALSE(s.isBlacklisted(8));
  EXPECT_TRUE(s.blacklist(8));
  EXPECT_TRUE(s.isBlacklisted(8));
  EXPECT_FALSE(s.isBlacklisted(9));
}

TEST(State, BlacklistIsIdempotent) {
  FakeStore store;
  OtaState s(store);
  ASSERT_TRUE(s.blacklist(8));
  const int writes = store.writeCount;
  EXPECT_TRUE(s.blacklist(8));
  EXPECT_EQ(store.writeCount, writes);
}

TEST(State, NinthBuildEvictsOldest) {
  FakeStore store;
  OtaState s(store);
  for (uint32_t b = 1; b <= 9; ++b) ASSERT_TRUE(s.blacklist(b));
  EXPECT_FALSE(s.isBlacklisted(1));
  for (uint32_t b = 2; b <= 9; ++b) EXPECT_TRUE(s.isBlacklisted(b)) << b;
  ASSERT_TRUE(s.blacklist(10));  // order preserved: 2 is now the oldest
  EXPECT_FALSE(s.isBlacklisted(2));
  for (uint32_t b = 3; b <= 10; ++b) EXPECT_TRUE(s.isBlacklisted(b)) << b;
}

TEST(State, BlacklistEntriesListOldestFirst) {
  FakeStore store;
  OtaState s(store);
  uint32_t out[OtaState::kBlacklistCapacity];
  EXPECT_EQ(s.blacklistEntries(out), 0u);
  ASSERT_TRUE(s.blacklist(3));
  ASSERT_TRUE(s.blacklist(4));
  ASSERT_EQ(s.blacklistEntries(out), 2u);
  EXPECT_EQ(out[0], 3u);
  EXPECT_EQ(out[1], 4u);
}

TEST(State, BlacklistSurvivesANewStateObject) {
  FakeStore store;
  {
    OtaState s(store);
    ASSERT_TRUE(s.blacklist(8));
  }
  OtaState again(store);
  EXPECT_TRUE(again.isBlacklisted(8));
}

TEST(State, EtagRoundTripAndTruncationRefused) {
  FakeStore store;
  OtaState s(store);
  char out[80];
  EXPECT_EQ(s.etag(out, sizeof(out)), 0u);
  ASSERT_TRUE(s.setEtag("\"abc123\""));
  ASSERT_EQ(s.etag(out, sizeof(out)), 8u);
  EXPECT_STREQ(out, "\"abc123\"");
  const std::string max(64, 'e');
  EXPECT_TRUE(s.setEtag(max.c_str()));
  const std::string over(65, 'e');
  EXPECT_FALSE(s.setEtag(over.c_str()));
  ASSERT_EQ(s.etag(out, sizeof(out)), 64u);  // the refused write left the old value alone
  EXPECT_STREQ(out, max.c_str());
  EXPECT_FALSE(s.setEtag(nullptr));
}

TEST(State, EtagThatDoesNotFitTheCallerBufferReadsAsNone) {
  FakeStore store;
  OtaState s(store);
  ASSERT_TRUE(s.setEtag("\"abc123\""));
  char small[4] = {'x', 'x', 'x', 'x'};
  EXPECT_EQ(s.etag(small, sizeof(small)), 0u);
  EXPECT_EQ(small[0], '\0');
  EXPECT_EQ(s.etag(nullptr, 0), 0u);
}

TEST(State, ClearEtag) {
  FakeStore store;
  OtaState s(store);
  ASSERT_TRUE(s.setEtag("x"));
  EXPECT_TRUE(s.clearEtag());
  char out[8];
  EXPECT_EQ(s.etag(out, sizeof(out)), 0u);
}

TEST(State, StoreNotOkReportsNotOkAndWritesFail) {
  FakeStore store;
  store.okValue = false;
  OtaState s(store);
  EXPECT_FALSE(s.ok());
  EXPECT_FALSE(s.raiseFloor(5));
  EXPECT_FALSE(s.setPending(5));
  EXPECT_FALSE(s.clearPending());
  EXPECT_FALSE(s.blacklist(5));
  EXPECT_FALSE(s.setEtag("x"));
  EXPECT_FALSE(s.clearEtag());
  EXPECT_EQ(store.writeCount, 0);
  EXPECT_TRUE(store.uints.empty());
  EXPECT_TRUE(store.blobs.empty());
}

TEST(State, WriteFailuresAreReported) {
  FakeStore store;
  OtaState s(store);
  store.failWrites = true;
  EXPECT_FALSE(s.raiseFloor(5));
  EXPECT_FALSE(s.setPending(5));
  EXPECT_FALSE(s.blacklist(5));
  EXPECT_FALSE(s.setEtag("x"));
}

TEST(State, CorruptBlacklistBlobReadsAsEmpty) {
  FakeStore store;
  OtaState s(store);
  store.blobs["blacklist"] = std::vector<uint8_t>(7, 0xFF);  // wrong length
  EXPECT_FALSE(s.isBlacklisted(0xFFFFFFFFu));
  EXPECT_TRUE(s.blacklist(4));  // recovers by starting a fresh list
  EXPECT_TRUE(s.isBlacklisted(4));

  // Right length but an impossible count.
  std::vector<uint8_t> bad(4 + 4 * 8, 0);
  bad[0] = 200;
  store.blobs["blacklist"] = bad;
  EXPECT_FALSE(s.isBlacklisted(0));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ota_core/updater.h"
#include "support/ota_fakes.h"

using namespace ota_core;
using namespace ota_fakes;

namespace {

constexpr uint32_t kRunning = 8;

struct Rig {
  FakeStore store;
  FakeSlots slots;
  FakeHttp http;
  Log log;
  Config cfg{};
  Rig() {
    store.log = &log;
    slots.log = &log;
    cfg.basePath = "/planeradar";
    cfg.deviceId[0] = 1;
    cfg.runningBuild = kRunning;
  }
  Updater make() { return Updater(cfg, store, slots, http); }
};

bool verifyNever(const uint8_t*, const uint8_t*, const uint8_t*, size_t) { return false; }

}  // namespace

TEST(Boot, CleanBootReportsNothing) {
  Rig r;
  r.store.uints["booted"] = kRunning;
  r.cfg.verify = verifyNever;
  Updater u = r.make();
  const BootInfo info = u.onBoot();
  EXPECT_FALSE(info.pendingVerify);
  EXPECT_FALSE(info.rolledBack);
  EXPECT_EQ(info.rolledBackBuild, 0u);
  EXPECT_EQ(r.store.writeCount, 0);
}

TEST(Boot, PendingForRunningBuildIsPendingVerify) {
  Rig r;
  r.store.uints["booted"] = kRunning;
  r.slots.state = SlotState::kPendingVerify;
  r.store.uints["pending"] = kRunning;
  Updater u = r.make();
  const BootInfo info = u.onBoot();
  EXPECT_TRUE(info.pendingVerify);
  EXPECT_FALSE(info.rolledBack);
  EXPECT_EQ(r.store.uints["pending"], kRunning);  // confirmBoot clears it, not onBoot
  EXPECT_EQ(r.store.writeCount, 0);
}

TEST(Boot, PendingForOtherBuildWithInvalidSlotRollsBackAndBlacklists) {
  Rig r;
  r.store.uints["pending"] = 9;
  r.slots.invalidSlot = true;
  Updater u = r.make();
  const BootInfo info = u.onBoot();
  EXPECT_TRUE(info.rolledBack);
  EXPECT_EQ(info.rolledBackBuild, 9u);
  EXPECT_FALSE(info.pendingVerify);
  OtaState s(r.store);
  EXPECT_TRUE(s.isBlacklisted(9));
  EXPECT_EQ(s.pending(), 0u);
  EXPECT_EQ(s.floor(), 0u);
}

TEST(Boot, PendingForOtherBuildWithoutInvalidSlotIsStaleAndCleared) {
  Rig r;
  r.store.uints["pending"] = 9;
  r.store.uints["floor"] = 4;
  Updater u = r.make();
  const BootInfo info = u.onBoot();
  EXPECT_FALSE(info.rolledBack);
  OtaState s(r.store);
  EXPECT_FALSE(s.isBlacklisted(9));
  EXPECT_EQ(s.pending(), 0u);
  EXPECT_EQ(s.floor(), 4u);
}

TEST(Boot, RollbackWithStoreNotOkDoesNothing) {
  Rig r;
  r.store.uints["pending"] = 9;
  r.store.okValue = false;
  r.slots.invalidSlot = true;
  Updater u = r.make();
  const BootInfo info = u.onBoot();
  EXPECT_FALSE(info.rolledBack);  // cannot record it, so it is not reported as handled
  EXPECT_EQ(r.store.writeCount, 0);
}

TEST(Boot, FirstBootOfADifferentBuildForgetsTheEtag) {
  // A USB flash of another build keeps NVS, so a stored ETag would answer 304 forever.
  Rig r;
  r.store.uints["booted"] = 6;
  ASSERT_TRUE(OtaState(r.store).setEtag("\"v8\""));
  Updater u = r.make();
  u.onBoot();
  char buf[16];
  EXPECT_EQ(OtaState(r.store).etag(buf, sizeof(buf)), 0u);
  EXPECT_EQ(r.store.uints["booted"], kRunning);
  EXPECT_EQ(OtaState(r.store).floor(), 0u);
}

TEST(Boot, SameBuildKeepsTheEtag) {
  Rig r;
  r.store.uints["booted"] = kRunning;
  ASSERT_TRUE(OtaState(r.store).setEtag("\"v8\""));
  Updater u = r.make();
  u.onBoot();
  char buf[16];
  EXPECT_EQ(OtaState(r.store).etag(buf, sizeof(buf)), 4u);
}

TEST(Boot, OnBootNeverRaisesTheFloor) {
  Rig r;
  r.slots.state = SlotState::kPendingVerify;
  r.store.uints["pending"] = kRunning;
  Updater u = r.make();
  u.onBoot();
  EXPECT_EQ(OtaState(r.store).floor(), 0u);
}

TEST(Confirm, MarksValidThenRaisesFloorAndClearsPending) {
  Rig r;
  r.slots.state = SlotState::kPendingVerify;
  r.store.uints["pending"] = kRunning;
  Updater u = r.make();
  ASSERT_TRUE(u.confirmBoot());
  EXPECT_EQ(r.log, (Log{"markRunningValid", "putUInt:floor", "erase:pending"}));
  OtaState s(r.store);
  EXPECT_EQ(s.floor(), kRunning);
  EXPECT_EQ(s.pending(), 0u);
  EXPECT_EQ(r.slots.state, SlotState::kValid);
}

TEST(Confirm, WhenAlreadyValidWritesNothing) {
  Rig r;
  r.slots.state = SlotState::kValid;
  Updater u = r.make();
  EXPECT_TRUE(u.confirmBoot());
  EXPECT_TRUE(r.log.empty());
}

TEST(Confirm, FailureToMarkLeavesFloorAndPendingUnchanged) {
  Rig r;
  r.slots.state = SlotState::kPendingVerify;
  r.slots.markFails = true;
  r.store.uints["pending"] = kRunning;
  Updater u = r.make();
  EXPECT_FALSE(u.confirmBoot());
  EXPECT_EQ(r.store.writeCount, 0);
  EXPECT_EQ(r.store.uints["pending"], kRunning);
}

TEST(Confirm, StoreNotOkDoesNotMarkValid) {
  Rig r;
  r.slots.state = SlotState::kPendingVerify;
  r.store.okValue = false;
  Updater u = r.make();
  EXPECT_FALSE(u.confirmBoot());
  EXPECT_EQ(r.slots.state, SlotState::kPendingVerify);
  EXPECT_TRUE(r.log.empty());
}

TEST(Confirm, ReportsFailureWhenTheFloorWriteFails) {
  Rig r;
  r.slots.state = SlotState::kPendingVerify;
  Updater u = r.make();
  r.store.failWrites = true;  // marking succeeds, the floor write does not
  EXPECT_FALSE(u.confirmBoot());
}

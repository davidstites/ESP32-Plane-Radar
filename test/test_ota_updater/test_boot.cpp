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
  r.store.uints["activated"] = 9;
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

TEST(Boot, AnInstallThatWasNeverActivatedIsStaleEvenWithALeftoverInvalidSlot) {
  // Power cut between setPending and endUpdate, with an old rollback's invalid slot still marked.
  Rig r;
  r.store.uints["pending"] = 9;
  r.slots.invalidSlot = true;
  Updater u = r.make();
  const BootInfo info = u.onBoot();
  EXPECT_FALSE(info.rolledBack);
  EXPECT_FALSE(OtaState(r.store).isBlacklisted(9));
  EXPECT_EQ(OtaState(r.store).pending(), 0u);
}

TEST(Boot, ClearingPendingAlsoClearsItsActivationRecord) {
  Rig r;
  r.store.uints["pending"] = 9;
  r.store.uints["activated"] = 9;
  r.slots.invalidSlot = true;
  Updater u = r.make();
  u.onBoot();
  EXPECT_EQ(r.store.uints.count("activated"), 0u);
}

TEST(Boot, FirstUnhealthyRollbackIsForgivenNotBlacklisted) {
  // The build ran but never confirmed (e.g. the ADS-B API was down): retry it once.
  Rig r;
  r.store.uints["pending"] = 9;
  r.store.uints["activated"] = 9;
  r.store.uints["soft"] = 9;
  r.slots.invalidSlot = true;
  Updater u = r.make();
  const BootInfo info = u.onBoot();
  EXPECT_TRUE(info.rolledBack);
  EXPECT_TRUE(info.forgiven);
  EXPECT_EQ(info.rolledBackBuild, 9u);
  OtaState s(r.store);
  EXPECT_FALSE(s.isBlacklisted(9));
  EXPECT_EQ(s.pending(), 0u);
  EXPECT_EQ(r.store.uints["softseen"], 9u);
  EXPECT_EQ(r.store.uints.count("soft"), 0u);
}

TEST(Boot, SecondUnhealthyRollbackOfTheSameBuildIsBlacklisted) {
  Rig r;
  r.store.uints["pending"] = 9;
  r.store.uints["activated"] = 9;
  r.store.uints["soft"] = 9;
  r.store.uints["softseen"] = 9;
  r.slots.invalidSlot = true;
  Updater u = r.make();
  const BootInfo info = u.onBoot();
  EXPECT_TRUE(info.rolledBack);
  EXPECT_FALSE(info.forgiven);
  EXPECT_TRUE(OtaState(r.store).isBlacklisted(9));
  EXPECT_EQ(r.store.uints.count("soft"), 0u);
}

TEST(Boot, ACrashOrHangRollbackIsBlacklistedEvenAfterAForgiveness) {
  Rig r;
  r.store.uints["pending"] = 9;
  r.store.uints["activated"] = 9;
  r.store.uints["softseen"] = 9;  // no "soft" record: the board did not restart itself
  r.slots.invalidSlot = true;
  Updater u = r.make();
  EXPECT_FALSE(u.onBoot().forgiven);
  EXPECT_TRUE(OtaState(r.store).isBlacklisted(9));
}

TEST(Boot, ForgivenessFallsBackToBlacklistWhenItCannotBeRecorded) {
  Rig r;
  r.store.uints["pending"] = 9;
  r.store.uints["activated"] = 9;
  r.store.uints["soft"] = 9;
  r.slots.invalidSlot = true;
  r.store.failWrites = true;  // nothing can be written, so nothing is claimed as handled
  Updater u = r.make();
  const BootInfo info = u.onBoot();
  EXPECT_FALSE(info.forgiven);
  EXPECT_FALSE(info.rolledBack);
}

TEST(Confirm, ConfirmingClearsTheForgivenessRecord) {
  Rig r;
  r.slots.state = SlotState::kPendingVerify;
  r.store.uints["pending"] = kRunning;
  r.store.uints["softseen"] = kRunning;
  Updater u = r.make();
  ASSERT_TRUE(u.confirmBoot());
  EXPECT_EQ(r.store.uints.count("softseen"), 0u);
}

TEST(NoteUnhealthy, RecordsTheRunningBuildWhileItIsUnconfirmed) {
  Rig r;
  r.slots.state = SlotState::kPendingVerify;
  Updater u = r.make();
  EXPECT_TRUE(u.noteUnhealthy());
  EXPECT_EQ(r.store.uints["soft"], kRunning);
}

TEST(NoteUnhealthy, DoesNothingForAnAlreadyConfirmedImage) {
  Rig r;
  Updater u = r.make();
  EXPECT_FALSE(u.noteUnhealthy());
  EXPECT_EQ(r.store.writeCount, 0);
}

TEST(NoteUnhealthy, ReportsAFailedWrite) {
  Rig r;
  r.slots.state = SlotState::kPendingVerify;
  r.store.failWrites = true;
  Updater u = r.make();
  EXPECT_FALSE(u.noteUnhealthy());
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

TEST(Confirm, AnAlreadyValidImageStillFinishesAnInterruptedConfirm) {
  Rig r;
  r.slots.state = SlotState::kValid;  // marked valid, but the floor write failed last time
  r.store.uints["pending"] = kRunning;
  Updater u = r.make();
  EXPECT_TRUE(u.confirmBoot());
  EXPECT_EQ(r.log, (Log{"putUInt:floor", "erase:pending"}));
  EXPECT_EQ(OtaState(r.store).floor(), kRunning);
  EXPECT_EQ(OtaState(r.store).pending(), 0u);
}

TEST(Confirm, AnAlreadyValidImageReportsFailureIfTheInterruptedConfirmStillCannotWrite) {
  Rig r;
  r.slots.state = SlotState::kValid;
  r.store.uints["pending"] = kRunning;
  Updater u = r.make();
  r.store.failWrites = true;
  EXPECT_FALSE(u.confirmBoot());
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

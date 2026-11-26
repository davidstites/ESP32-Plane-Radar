#include <gtest/gtest.h>

#include "ota_core/wdt_gate.h"

using ota_core::LoopWdtGate;

TEST(LoopWdtGate, DoesNotFeedBeforeArmed) {
  LoopWdtGate gate;
  EXPECT_FALSE(gate.shouldFeed());  // feeding an unarmed loop watchdog logs error 261
}

TEST(LoopWdtGate, FeedsWhileArmed) {
  LoopWdtGate gate;
  gate.arm();
  EXPECT_TRUE(gate.shouldFeed());
}

TEST(LoopWdtGate, StopsFeedingOnceDisarmed) {
  LoopWdtGate gate;
  gate.arm();
  gate.disarm();
  EXPECT_FALSE(gate.shouldFeed());  // the watchdog is disabled after confirmation
}

TEST(LoopWdtGate, ArmIsIdempotentAndRearmable) {
  LoopWdtGate gate;
  EXPECT_TRUE(gate.arm());   // first arm: the caller must enable the watchdog
  EXPECT_FALSE(gate.arm());  // already armed: nothing to do
  gate.disarm();
  EXPECT_TRUE(gate.arm());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

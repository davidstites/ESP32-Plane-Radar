#include <gtest/gtest.h>

#include "button_logic/button_tracker.h"

using button_logic::ButtonTracker;

namespace {
constexpr uint32_t kTapMin = 40;
constexpr uint32_t kHold = 3000;
}  // namespace

TEST(ButtonTracker, ReleaseInsideTapWindowLatchesTap) {
  ButtonTracker b(kTapMin, kHold);
  b.onEdge(true, 1000);
  b.onEdge(false, 1200);
  EXPECT_TRUE(b.consumeTap());
}

TEST(ButtonTracker, ConsumeTapClearsIt) {
  ButtonTracker b(kTapMin, kHold);
  b.onEdge(true, 0);
  b.onEdge(false, 100);
  EXPECT_TRUE(b.consumeTap());
  EXPECT_FALSE(b.consumeTap());
}

TEST(ButtonTracker, BounceShorterThanMinimumIsNotATap) {
  ButtonTracker b(kTapMin, kHold);
  b.onEdge(true, 0);
  b.onEdge(false, kTapMin - 1);
  EXPECT_FALSE(b.consumeTap());
}

TEST(ButtonTracker, TapWindowBoundaries) {
  ButtonTracker at_min(kTapMin, kHold);
  at_min.onEdge(true, 0);
  at_min.onEdge(false, kTapMin);
  EXPECT_TRUE(at_min.consumeTap());

  ButtonTracker just_under_hold(kTapMin, kHold);
  just_under_hold.onEdge(true, 0);
  just_under_hold.onEdge(false, kHold - 1);
  EXPECT_TRUE(just_under_hold.consumeTap());

  ButtonTracker at_hold(kTapMin, kHold);
  at_hold.onEdge(true, 0);
  at_hold.onEdge(false, kHold);
  EXPECT_FALSE(at_hold.consumeTap());
}

TEST(ButtonTracker, ReleaseWithoutPressIsIgnored) {
  ButtonTracker b(kTapMin, kHold);
  b.onEdge(false, 500);
  EXPECT_FALSE(b.consumeTap());
}

TEST(ButtonTracker, SecondReleaseAfterTapIsIgnored) {
  ButtonTracker b(kTapMin, kHold);
  b.onEdge(true, 0);
  b.onEdge(false, 100);
  EXPECT_TRUE(b.consumeTap());
  b.onEdge(false, 200);
  EXPECT_FALSE(b.consumeTap());
}

TEST(ButtonTracker, TapSurvivesPollWhileReleased) {
  ButtonTracker b(kTapMin, kHold);
  b.onEdge(true, 0);
  b.onEdge(false, 100);
  EXPECT_FALSE(b.pollHeld(false, 150));
  EXPECT_TRUE(b.consumeTap());
}

TEST(ButtonTracker, LongPressFiresOnceAtHoldThreshold) {
  ButtonTracker b(kTapMin, kHold);
  b.onEdge(true, 0);
  EXPECT_FALSE(b.pollHeld(true, kHold - 1));
  EXPECT_TRUE(b.pollHeld(true, kHold));
  EXPECT_FALSE(b.pollHeld(true, kHold + 500));
}

TEST(ButtonTracker, LongPressDoesNotLatchTapOnRelease) {
  ButtonTracker b(kTapMin, kHold);
  b.onEdge(true, 0);
  EXPECT_TRUE(b.pollHeld(true, kHold));
  b.onEdge(false, kHold + 10);
  EXPECT_FALSE(b.consumeTap());
}

TEST(ButtonTracker, LongPressRearmsAfterRelease) {
  ButtonTracker b(kTapMin, kHold);
  b.onEdge(true, 0);
  EXPECT_TRUE(b.pollHeld(true, kHold));
  EXPECT_FALSE(b.pollHeld(false, kHold + 10));
  b.onEdge(true, 10000);
  EXPECT_TRUE(b.pollHeld(true, 10000 + kHold));
}

TEST(ButtonTracker, PollStartsTimingWhenEdgeWasMissed) {
  // Held since power-up: no edge ever arrived, so polling begins the timer.
  ButtonTracker b(kTapMin, kHold);
  EXPECT_FALSE(b.pollHeld(true, 5000));
  EXPECT_FALSE(b.pollHeld(true, 5000 + kHold - 1));
  EXPECT_TRUE(b.pollHeld(true, 5000 + kHold));
}

TEST(ButtonTracker, PollReleaseDropsPressSoLateReleaseEdgeIsNotATap) {
  ButtonTracker b(kTapMin, kHold);
  b.onEdge(true, 0);
  EXPECT_FALSE(b.pollHeld(false, 100));
  b.onEdge(false, 120);
  EXPECT_FALSE(b.consumeTap());
}

TEST(ButtonTracker, HoldMeasuredAcrossClockWraparound) {
  ButtonTracker b(kTapMin, kHold);
  const uint32_t start = 0xFFFFFFFFu - 1000;
  b.onEdge(true, start);
  EXPECT_FALSE(b.pollHeld(true, start + kHold - 1));
  EXPECT_TRUE(b.pollHeld(true, start + kHold));  // wraps past 0

  ButtonTracker tap(kTapMin, kHold);
  tap.onEdge(true, start);
  tap.onEdge(false, start + 200);
  EXPECT_TRUE(tap.consumeTap());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

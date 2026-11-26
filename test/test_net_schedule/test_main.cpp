#include <gtest/gtest.h>

#include <climits>

#include "net_schedule/scheduler.h"

using net_schedule::Action;
using net_schedule::Config;
using net_schedule::Scheduler;

namespace {

constexpr unsigned long kOtaFirst = 300000;       // 5 min
constexpr unsigned long kOtaInterval = 21600000;  // 6 h
constexpr unsigned long kOtaRetry = 1800000;      // 30 min
constexpr unsigned long kOtaQuick = 120000;       // 2 min

constexpr Config kConfig = {5000, 900000, 20000, 3000, kOtaFirst, kOtaInterval, kOtaRetry, kOtaQuick};

Scheduler readyScheduler() {
  Scheduler s(kConfig);
  s.setFeedsReady(true);
  return s;
}

}  // namespace

TEST(Scheduler, PollsAdsbImmediatelyOnFirstRun) {
  Scheduler s(kConfig);
  EXPECT_EQ(s.next(0), Action::kPollAdsb);
}

TEST(Scheduler, WaitsOutTheAdsbInterval) {
  Scheduler s(kConfig);
  s.adsbFinished(1000);
  EXPECT_EQ(s.next(1001), Action::kWait);
  EXPECT_EQ(s.next(5999), Action::kWait);
  EXPECT_EQ(s.next(6000), Action::kPollAdsb);
}

TEST(Scheduler, SleepIsCappedSoStateChangesAreNoticed) {
  Scheduler s(kConfig);
  s.adsbFinished(0);
  EXPECT_EQ(s.sleepMs(0), net_schedule::kMaxSleepMs);
}

TEST(Scheduler, SleepShrinksToTheNextDueTime) {
  Scheduler s(kConfig);
  s.adsbFinished(0);
  EXPECT_EQ(s.sleepMs(4900), 100u);
  EXPECT_EQ(s.sleepMs(5000), 0u);
}

TEST(Scheduler, NoFeedsUntilReady) {
  Scheduler s(kConfig);
  s.adsbFinished(0);
  EXPECT_EQ(s.next(100000), Action::kPollAdsb);
  EXPECT_EQ(s.next(1), Action::kWait);
}

TEST(Scheduler, FeedsRunFirstOnceReadyAndBeatAdsb) {
  Scheduler s = readyScheduler();
  EXPECT_EQ(s.next(0), Action::kRefreshFeeds);  // both due: feeds win
  s.feedsFinished(10, true);
  EXPECT_EQ(s.next(10), Action::kPollAdsb);
}

TEST(Scheduler, RefreshesFeedsAfterTheSuccessInterval) {
  Scheduler s = readyScheduler();
  s.otaFinished(0, true);  // the 15-minute window outlasts the 5-minute first OTA check
  s.feedsFinished(1000, true);
  s.adsbFinished(1000);
  EXPECT_EQ(s.next(1000 + 899999), Action::kPollAdsb);  // adsb due, feeds not yet
  s.adsbFinished(1000 + 899999);
  EXPECT_EQ(s.next(1000 + 900000), Action::kRefreshFeeds);
}

TEST(Scheduler, RetriesSoonerAfterAFailedRefresh) {
  Scheduler s = readyScheduler();
  s.adsbFinished(19500);  // ADS-B not due in the window being checked
  s.feedsFinished(0, false);
  EXPECT_EQ(s.next(19999), Action::kWait);
  EXPECT_EQ(s.next(20000), Action::kRefreshFeeds);
}

TEST(Scheduler, ViewChangeRefetchesAfterItSettles) {
  Scheduler s = readyScheduler();
  s.adsbFinished(0);
  s.feedsFinished(0, true);
  s.onViewChanged(4000);
  EXPECT_EQ(s.next(6999), Action::kPollAdsb);  // adsb due at 5000; feeds still settling
  s.adsbFinished(6999);
  EXPECT_EQ(s.next(7000), Action::kRefreshFeeds);
}

TEST(Scheduler, RepeatedViewChangesKeepPostponingTheRefetch) {
  Scheduler s = readyScheduler();
  s.adsbFinished(3500);  // ADS-B not due in the window being checked
  s.feedsFinished(0, true);
  s.onViewChanged(1000);
  s.onViewChanged(3500);  // the quiet period restarts
  EXPECT_EQ(s.next(3500 + 2999), Action::kWait);
  EXPECT_EQ(s.next(3500 + 3000), Action::kRefreshFeeds);
}

TEST(Scheduler, ViewChangeDuringARefreshRearmsTheNextOne) {
  Scheduler s = readyScheduler();
  s.adsbFinished(0);
  s.onViewChanged(100);
  s.feedsStarted();
  s.onViewChanged(500);  // changed again while fetching
  s.feedsFinished(900, true);
  EXPECT_EQ(s.next(3499), Action::kWait);
  EXPECT_EQ(s.next(3500), Action::kRefreshFeeds);
}

TEST(Scheduler, ViewChangeBeforeAStartedRefreshIsConsumedByIt) {
  Scheduler s = readyScheduler();
  s.adsbFinished(0);
  s.onViewChanged(100);
  s.feedsStarted();
  s.feedsFinished(900, true);
  EXPECT_EQ(s.next(60000), Action::kPollAdsb);  // nothing pending: feeds wait out 15 min
}

TEST(Scheduler, SleepIsBoundedByTheFeedSettleTime) {
  Scheduler s = readyScheduler();
  s.adsbFinished(0);
  s.feedsFinished(0, true);
  s.onViewChanged(4900);
  EXPECT_EQ(s.sleepMs(4950), 50u);  // adsb due at 5000 is nearer than the 7900 settle
  s.adsbFinished(4950);
  EXPECT_EQ(s.sleepMs(7800), 100u);
}

TEST(Scheduler, SurvivesMillisWrapping) {
  Scheduler s(kConfig);
  const unsigned long near_wrap = ULONG_MAX - 1000;
  s.adsbFinished(near_wrap);
  EXPECT_EQ(s.next(near_wrap + 4999), Action::kWait);  // wraps past ULONG_MAX
  EXPECT_EQ(s.next(near_wrap + 5000), Action::kPollAdsb);
}

TEST(SchedulerOta, NotDueBeforeFirstDelay) {
  Scheduler s(kConfig);
  EXPECT_EQ(s.next(kOtaFirst - 1), Action::kPollAdsb);  // adsb is due, ota is not
  s.adsbFinished(kOtaFirst - 1);
  EXPECT_EQ(s.next(kOtaFirst), Action::kCheckOta);
  EXPECT_NE(s.next(kOtaFirst - 1), Action::kCheckOta);
}

TEST(SchedulerOta, DueAtFirstDelay) {
  Scheduler s(kConfig);
  s.adsbFinished(kOtaFirst - 100);
  EXPECT_EQ(s.next(kOtaFirst), Action::kCheckOta);
  EXPECT_EQ(s.next(kOtaFirst + 12345), Action::kCheckOta);
}

TEST(SchedulerOta, BeatsAdsbAndFeeds) {
  Scheduler s = readyScheduler();  // feeds ready and never run: due
  EXPECT_EQ(s.next(kOtaFirst), Action::kCheckOta);
}

TEST(SchedulerOta, SuccessWaitsIntervalPlusJitter) {
  Scheduler s(kConfig);
  s.setOtaJitterMs(1000000);
  s.otaStarted();
  s.otaFinished(kOtaFirst, true);
  s.adsbFinished(kOtaFirst + kOtaInterval + 999999);
  const unsigned long due = kOtaFirst + kOtaInterval + 1000000;
  EXPECT_NE(s.next(due - 1), Action::kCheckOta);
  EXPECT_EQ(s.next(due), Action::kCheckOta);
}

TEST(SchedulerOta, JitterZeroMeansExactInterval) {
  Scheduler s(kConfig);
  s.setOtaJitterMs(0);
  s.otaStarted();
  s.otaFinished(kOtaFirst, true);
  s.adsbFinished(kOtaFirst + kOtaInterval - 1);
  EXPECT_NE(s.next(kOtaFirst + kOtaInterval - 1), Action::kCheckOta);
  EXPECT_EQ(s.next(kOtaFirst + kOtaInterval), Action::kCheckOta);
}

TEST(SchedulerOta, FailureRetriesAfterRetryGapWithoutJitter) {
  Scheduler s(kConfig);
  s.setOtaJitterMs(1000000);
  s.otaStarted();
  s.otaFinished(kOtaFirst, false);
  s.adsbFinished(kOtaFirst + kOtaRetry - 1);
  EXPECT_NE(s.next(kOtaFirst + kOtaRetry - 1), Action::kCheckOta);
  EXPECT_EQ(s.next(kOtaFirst + kOtaRetry), Action::kCheckOta);
}

TEST(SchedulerOta, TransientFailureRetriesQuickly) {
  Scheduler s(kConfig);
  s.otaStarted();
  s.otaFinished(kOtaFirst, false, true);
  s.adsbFinished(kOtaFirst + kOtaQuick - 1);
  EXPECT_NE(s.next(kOtaFirst + kOtaQuick - 1), Action::kCheckOta);
  EXPECT_EQ(s.next(kOtaFirst + kOtaQuick), Action::kCheckOta);
}

TEST(SchedulerOta, QuickRetriesAreCappedThenFallBackToTheLongGap) {
  Scheduler s(kConfig);
  unsigned long now = kOtaFirst;
  for (int i = 0; i < net_schedule::kOtaQuickRetries; ++i) {
    s.otaStarted();
    s.otaFinished(now, false, true);
    now += kOtaQuick;
    s.adsbFinished(now);
    ASSERT_EQ(s.next(now), Action::kCheckOta) << "quick retry " << i;
  }
  s.otaStarted();
  s.otaFinished(now, false, true);  // the cap is spent: wait the long gap
  s.adsbFinished(now + kOtaRetry - 1);
  EXPECT_NE(s.next(now + kOtaRetry - 1), Action::kCheckOta);
  EXPECT_EQ(s.next(now + kOtaRetry), Action::kCheckOta);
}

TEST(SchedulerOta, SuccessResetsTheQuickRetryCount) {
  Scheduler s(kConfig);
  s.setOtaJitterMs(0);
  for (int i = 0; i < net_schedule::kOtaQuickRetries; ++i) {
    s.otaStarted();
    s.otaFinished(kOtaFirst, false, true);
  }
  s.otaStarted();
  s.otaFinished(kOtaFirst, true);
  s.otaStarted();
  s.otaFinished(kOtaFirst, false, true);  // counts as a first quick retry again
  s.adsbFinished(kOtaFirst + kOtaQuick);
  EXPECT_EQ(s.next(kOtaFirst + kOtaQuick), Action::kCheckOta);
}

TEST(SchedulerOta, AnOrdinaryFailureResetsTheQuickRetryCount) {
  Scheduler s(kConfig);
  for (int i = 0; i < net_schedule::kOtaQuickRetries; ++i) {
    s.otaStarted();
    s.otaFinished(kOtaFirst, false, true);
  }
  s.otaStarted();
  s.otaFinished(kOtaFirst, false, false);
  s.otaStarted();
  s.otaFinished(kOtaFirst, false, true);
  s.adsbFinished(kOtaFirst + kOtaQuick);
  EXPECT_EQ(s.next(kOtaFirst + kOtaQuick), Action::kCheckOta);
}

TEST(SchedulerOta, NotDueWhileACheckIsRunning) {
  Scheduler s(kConfig);
  s.adsbFinished(kOtaFirst);
  ASSERT_EQ(s.next(kOtaFirst), Action::kCheckOta);
  s.otaStarted();
  EXPECT_NE(s.next(kOtaFirst + 1), Action::kCheckOta);
  EXPECT_EQ(s.sleepMs(kOtaFirst + 1), 250u);
}

TEST(SchedulerOta, SurvivesMillisWrap) {
  Scheduler s(kConfig);
  s.setOtaJitterMs(0);
  const unsigned long near_wrap = ULONG_MAX - 1000;
  s.otaStarted();
  s.otaFinished(near_wrap, true);
  s.adsbFinished(near_wrap + kOtaInterval - 1);
  EXPECT_NE(s.next(near_wrap + kOtaInterval - 1), Action::kCheckOta);
  EXPECT_EQ(s.next(near_wrap + kOtaInterval), Action::kCheckOta);  // wrapped past ULONG_MAX
}

TEST(SchedulerOta, SleepShrinksToTheOtaDueTime) {
  Scheduler s(kConfig);
  s.adsbFinished(kOtaFirst - 100);
  EXPECT_EQ(s.sleepMs(kOtaFirst - 50), 50u);
}

TEST(SchedulerOta, FeedsAndAdsbStillRunAroundIt) {
  Scheduler s = readyScheduler();
  s.otaStarted();
  s.otaFinished(0, true);
  EXPECT_EQ(s.next(0), Action::kRefreshFeeds);
  s.feedsFinished(0, true);
  EXPECT_EQ(s.next(0), Action::kPollAdsb);
}

TEST(RetryBackoff, GrowsLinearlyAndNeverNegative) {
  EXPECT_EQ(net_schedule::retryBackoffMs(0), 0u);
  EXPECT_EQ(net_schedule::retryBackoffMs(-1), 0u);
  EXPECT_EQ(net_schedule::retryBackoffMs(1), 2000u);
  EXPECT_EQ(net_schedule::retryBackoffMs(2), 4000u);
  EXPECT_EQ(net_schedule::kFeedAttempts, 3);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

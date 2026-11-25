#include "net_schedule/scheduler.h"

#include <algorithm>

namespace net_schedule {
namespace {

/** Milliseconds left of `interval` that began at `since`; 0 once it has elapsed. */
unsigned long remaining(unsigned long now_ms, unsigned long since_ms, unsigned long interval_ms) {
  const unsigned long elapsed = now_ms - since_ms;
  return elapsed >= interval_ms ? 0UL : interval_ms - elapsed;
}

}  // namespace

void Scheduler::onViewChanged(unsigned long now_ms) {
  view_pending_ = true;
  view_changed_ms_ = now_ms;
}

unsigned long Scheduler::adsbRemaining(unsigned long now_ms) const {
  return adsb_ever_ ? remaining(now_ms, last_adsb_ms_, config_.adsb_interval_ms) : 0UL;
}

unsigned long Scheduler::feedsRemaining(unsigned long now_ms) const {
  if (!feeds_ready_) {
    return ULONG_MAX;
  }
  if (!feeds_ever_) {
    return 0UL;
  }
  const unsigned long gap = last_feed_ok_ ? config_.feed_interval_ms : config_.feed_retry_ms;
  unsigned long left = remaining(now_ms, last_feed_ms_, gap);
  if (view_pending_) {
    left = std::min(left, remaining(now_ms, view_changed_ms_, config_.view_settle_ms));
  }
  return left;
}

unsigned long Scheduler::otaRemaining(unsigned long now_ms) const {
  if (ota_running_) {
    return ULONG_MAX;
  }
  return ota_ever_ ? remaining(now_ms, last_ota_ms_, ota_gap_ms_)
                   : remaining(now_ms, 0UL, ota_probing_ ? kOtaProbeFirstMs : config_.ota_first_ms);
}

Action Scheduler::next(unsigned long now_ms) const {
  if (otaRemaining(now_ms) == 0) {
    return Action::kCheckOta;
  }
  if (feedsRemaining(now_ms) == 0) {
    return Action::kRefreshFeeds;
  }
  if (adsbRemaining(now_ms) == 0) {
    return Action::kPollAdsb;
  }
  return Action::kWait;
}

unsigned long Scheduler::sleepMs(unsigned long now_ms) const {
  return std::min({adsbRemaining(now_ms), feedsRemaining(now_ms), otaRemaining(now_ms), kMaxSleepMs});
}

void Scheduler::adsbFinished(unsigned long now_ms) {
  adsb_ever_ = true;
  last_adsb_ms_ = now_ms;
}

void Scheduler::otaFinished(unsigned long now_ms, bool ok) {
  ota_running_ = false;
  ota_ever_ = true;
  last_ota_ms_ = now_ms;
  if (ota_probing_) {
    ota_gap_ms_ = kOtaProbeRetryMs;
  } else {
    ota_gap_ms_ = ok ? config_.ota_interval_ms + ota_jitter_ms_ : config_.ota_retry_ms;
  }
}

void Scheduler::feedsStarted() { view_pending_ = false; }

void Scheduler::feedsFinished(unsigned long now_ms, bool ok) {
  feeds_ever_ = true;
  last_feed_ok_ = ok;
  last_feed_ms_ = now_ms;
}

}  // namespace net_schedule

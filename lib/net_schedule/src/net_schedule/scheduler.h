#pragma once

#include <climits>

namespace net_schedule {

/** What the single network task should do next. */
enum class Action { kWait, kPollAdsb, kRefreshFeeds };

struct Config {
  unsigned long adsb_interval_ms;
  /** Gap between successful weather refreshes. */
  unsigned long feed_interval_ms;
  /** Gap before trying again after a failed weather refresh. */
  unsigned long feed_retry_ms;
  /** A view change must stay unchanged this long before the feeds refetch for it. */
  unsigned long view_settle_ms;
};

/** Longest sleep, so a view change or Wi-Fi state flip is noticed promptly. */
constexpr unsigned long kMaxSleepMs = 250;
/** Attempts a feed makes within one refresh before giving up until the retry gap. */
constexpr int kFeedAttempts = 3;

/** Wait before attempt number `attempt` (1 = first retry) within one refresh. */
constexpr unsigned long retryBackoffMs(int attempt) {
  return attempt <= 0 ? 0UL : 2000UL * static_cast<unsigned long>(attempt);
}

/**
 * Decides, from millis() timestamps, which network job runs next. One task runs the
 * jobs in turn, so two TLS sessions never overlap. Pure state: no clock, no I/O, and
 * every comparison survives millis() wrapping.
 */
class Scheduler {
 public:
  explicit Scheduler(const Config& config) : config_(config) {}

  /** Feeds can run only once the view's airports are known. */
  void setFeedsReady(bool ready) { feeds_ready_ = ready; }

  /** The view (center or range) changed: refetch after view_settle_ms of quiet. */
  void onViewChanged(unsigned long now_ms);

  /** Feeds take priority over ADS-B when both are due. */
  Action next(unsigned long now_ms) const;

  /** Milliseconds to sleep when next() is kWait; never more than kMaxSleepMs. */
  unsigned long sleepMs(unsigned long now_ms) const;

  void adsbFinished(unsigned long now_ms);
  /** Call when a refresh starts; a view change during it re-arms the next one. */
  void feedsStarted();
  void feedsFinished(unsigned long now_ms, bool ok);

 private:
  unsigned long adsbRemaining(unsigned long now_ms) const;
  unsigned long feedsRemaining(unsigned long now_ms) const;

  Config config_;
  bool feeds_ready_ = false;
  bool adsb_ever_ = false;
  unsigned long last_adsb_ms_ = 0;
  bool feeds_ever_ = false;
  bool last_feed_ok_ = false;
  unsigned long last_feed_ms_ = 0;
  bool view_pending_ = false;
  unsigned long view_changed_ms_ = 0;
};

}  // namespace net_schedule

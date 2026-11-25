#pragma once

#include <climits>

namespace net_schedule {

/** What the single network task should do next. */
enum class Action { kWait, kPollAdsb, kRefreshFeeds, kCheckOta };

struct Config {
  unsigned long adsb_interval_ms;
  /** Gap between successful weather refreshes. */
  unsigned long feed_interval_ms;
  /** Gap before trying again after a failed weather refresh. */
  unsigned long feed_retry_ms;
  /** A view change must stay unchanged this long before the feeds refetch for it. */
  unsigned long view_settle_ms;
  /** Uptime at which the first firmware-update check runs. */
  unsigned long ota_first_ms;
  /** Gap after a successful check, before jitter. */
  unsigned long ota_interval_ms;
  /** Gap before trying again after a failed check. */
  unsigned long ota_retry_ms;
};

/** Longest sleep, so a view change or Wi-Fi state flip is noticed promptly. */
constexpr unsigned long kMaxSleepMs = 250;
/** While a new build awaits confirmation the OTA slot runs a health probe: soon, and every minute. */
constexpr unsigned long kOtaProbeFirstMs = 20000;
constexpr unsigned long kOtaProbeRetryMs = 60000;
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

  /**
   * A due firmware check goes first, then feeds, then ADS-B. Checks are rare, and keeping
   * them in this one task is what keeps TLS sessions from overlapping.
   */
  Action next(unsigned long now_ms) const;

  /** Milliseconds to sleep when next() is kWait; never more than kMaxSleepMs. */
  unsigned long sleepMs(unsigned long now_ms) const;

  void adsbFinished(unsigned long now_ms);
  /** Call when a refresh starts; a view change during it re-arms the next one. */
  void feedsStarted();
  void feedsFinished(unsigned long now_ms, bool ok);

  /** Extra delay added to the next successful-check gap, so a fleet does not check in step. */
  void setOtaJitterMs(unsigned long jitter_ms) { ota_jitter_ms_ = jitter_ms; }
  /** Probe mode: the OTA slot runs fast health probes until switched off, success or not. */
  void setOtaProbing(bool probing) { ota_probing_ = probing; }
  /** Call when a check starts; none is due again until otaFinished(). */
  void otaStarted() { ota_running_ = true; }
  /** Success waits ota_interval_ms plus the jitter; failure waits ota_retry_ms. */
  void otaFinished(unsigned long now_ms, bool ok);

 private:
  unsigned long adsbRemaining(unsigned long now_ms) const;
  unsigned long feedsRemaining(unsigned long now_ms) const;
  unsigned long otaRemaining(unsigned long now_ms) const;

  Config config_;
  bool feeds_ready_ = false;
  bool adsb_ever_ = false;
  unsigned long last_adsb_ms_ = 0;
  bool feeds_ever_ = false;
  bool last_feed_ok_ = false;
  unsigned long last_feed_ms_ = 0;
  bool view_pending_ = false;
  unsigned long view_changed_ms_ = 0;
  bool ota_running_ = false;
  bool ota_probing_ = false;
  bool ota_ever_ = false;
  unsigned long ota_jitter_ms_ = 0;
  unsigned long last_ota_ms_ = 0;
  unsigned long ota_gap_ms_ = 0;
};

}  // namespace net_schedule

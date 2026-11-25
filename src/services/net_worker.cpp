#include "services/net_worker.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_random.h>

#include <net_schedule/scheduler.h>
#include <radar_geo/radar_geo.h>

#include "config.h"
#include "services/adsb_client.h"
#include "services/crash_log.h"
#include "services/metar_client.h"
#include "services/ota_service.h"
#include "services/precip_client.h"
#include "services/radar_location.h"
#include "ui/radar_range.h"
#include "ui/runway_overlay.h"

namespace services::net {

namespace {

radar_geo::ViewKey currentView() {
  return {location::lat(), location::lon(), ui::radar::rangeCurrent().ring3_km};
}

/** METAR flight categories for the airports labeled on screen, retried on failure. */
bool refreshMetars() {
  const char* icaos[metar_parse::kMaxStations];
  const size_t count = ui::runway::snapshotLabelIcaos(icaos, metar_parse::kMaxStations);
  if (count == 0) {
    return true;  // no airport in view: nothing to ask
  }
  for (int attempt = 0; attempt < net_schedule::kFeedAttempts; ++attempt) {
    if (attempt > 0) {
      vTaskDelay(pdMS_TO_TICKS(net_schedule::retryBackoffMs(attempt)));
    }
    if (metar::fetch(icaos, count)) {
      return true;
    }
  }
  return false;
}

/**
 * Weather feeds, one TLS session at a time. The ADS-B session is closed first: its ~40 KB
 * of buffers plus another session's would not fit in the heap.
 */
bool refreshFeeds() {
  adsb::closeConnection();
  const bool metar_ok = refreshMetars();
  // One attempt: a failure is retried by the scheduler after kFeedRetryMs.
  const bool precip_ok = !ui::radar::showPrecip() ||
                         precip::fetch(currentView(), ui::radar::rangeCurrent().outer_km);
  Serial.printf("feeds: metar %s, precip %s; heap free %u (lowest ever %u), stack headroom %u B\n",
                metar_ok ? "ok" : "failed", precip_ok ? "ok" : "failed",
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getMinFreeHeap()),
                static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  return metar_ok && precip_ok;
}

// Pinned to core 0, away from the render loop on core 1. The HTTPS requests block for
// ~1-2 s; keeping them off the main loop lets the radar keep redrawing (dead-reckoned) at
// 4 Hz throughout.
void workerTask(void*) {
  net_schedule::Scheduler scheduler({config::kAdsbFetchIntervalMs, config::kFeedRefreshIntervalMs,
                                     config::kFeedRetryMs, config::kViewSettleMs, config::kOtaFirstCheckMs,
                                     config::kOtaIntervalMs, config::kOtaRetryMs});
  scheduler.setOtaProbing(services::ota::pendingVerify());  // a new build proves itself first
  radar_geo::ViewKey last_view = currentView();
  bool last_show_precip = ui::radar::showPrecip();
  for (;;) {
    // No fetching for the placeholder location while the setup QR screen is showing.
    if (WiFi.status() != WL_CONNECTED || !location::hasSaved()) {
      vTaskDelay(pdMS_TO_TICKS(net_schedule::kMaxSleepMs));
      continue;
    }

    const unsigned long now = millis();
    const radar_geo::ViewKey view = currentView();
    if (!radar_geo::sameView(view, last_view)) {
      scheduler.onViewChanged(now);
      last_view = view;
    }
    // Turning the radar layer on needs its image: treat it like a view change.
    const bool show_precip = ui::radar::showPrecip();
    if (show_precip && !last_show_precip) {
      scheduler.onViewChanged(now);
    }
    last_show_precip = show_precip;
    scheduler.setFeedsReady(ui::runway::labelsReady() || show_precip);

    switch (scheduler.next(now)) {
      case net_schedule::Action::kPollAdsb:
        services::crash_log::checkpoint("adsb_poll");
        adsb::fetchUpdate(location::lat(), location::lon(), ui::radar::fetchRadiusKm());
        scheduler.adsbFinished(millis());
        break;
      case net_schedule::Action::kRefreshFeeds:
        services::crash_log::checkpoint("feeds");
        scheduler.feedsStarted();
        scheduler.feedsFinished(millis(), refreshFeeds());
        break;
      case net_schedule::Action::kCheckOta: {
        services::crash_log::checkpoint("ota_check");
        adsb::closeConnection();  // free the kept-alive TLS session's heap first
        scheduler.otaStarted();
        if (services::ota::pendingVerify()) {
          const bool confirmed = services::ota::probeNow();
          if (confirmed) scheduler.setOtaProbing(false);  // back to the 6-12 h update checks
          scheduler.otaFinished(millis(), confirmed);
          break;
        }
        const ota_core::CheckResult result = services::ota::checkNow();
        scheduler.setOtaJitterMs(esp_random() % config::kOtaJitterMaxMs);
        scheduler.otaFinished(millis(), result.outcome != ota_core::Outcome::kFailed);
        if (result.outcome == ota_core::Outcome::kInstalled) {
          Serial.println("ota: restarting into the new build");
          vTaskDelay(pdMS_TO_TICKS(500));
          ESP.restart();
        }
        break;
      }
      case net_schedule::Action::kWait:
        vTaskDelay(pdMS_TO_TICKS(scheduler.sleepMs(now)));
        break;
    }
  }
}

}  // namespace

void start() { xTaskCreatePinnedToCore(workerTask, "net", 16384, nullptr, 1, nullptr, 0); }

}  // namespace services::net

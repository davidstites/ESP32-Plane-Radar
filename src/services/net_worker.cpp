#include "services/net_worker.h"

#include <Arduino.h>
#include <WiFi.h>

#include <net_schedule/scheduler.h>

#include "config.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "ui/radar_range.h"

namespace services::net {

namespace {

// Pinned to core 0, away from the render loop on core 1. The HTTPS request blocks for
// ~1-2 s; keeping it off the main loop lets the radar keep redrawing (dead-reckoned) at
// 4 Hz throughout.
void workerTask(void*) {
  net_schedule::Scheduler scheduler({config::kAdsbFetchIntervalMs, config::kFeedRefreshIntervalMs,
                                     config::kFeedRetryMs, config::kViewSettleMs});
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(net_schedule::kMaxSleepMs));
      continue;
    }

    const unsigned long now = millis();
    if (scheduler.next(now) == net_schedule::Action::kPollAdsb) {
      adsb::fetchUpdate(location::lat(), location::lon(), ui::radar::fetchRadiusKm());
      scheduler.adsbFinished(millis());
    } else {
      vTaskDelay(pdMS_TO_TICKS(scheduler.sleepMs(now)));
    }
  }
}

}  // namespace

void start() { xTaskCreatePinnedToCore(workerTask, "net", 16384, nullptr, 1, nullptr, 0); }

}  // namespace services::net

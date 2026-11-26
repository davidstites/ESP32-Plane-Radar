#include "services/crash_log.h"

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>

#include "config.h"
#include "crash_breadcrumb/crash_breadcrumb.h"
#include "services/ota_service.h"

namespace services::crash_log {

namespace {

RTC_NOINIT_ATTR crash_breadcrumb::Breadcrumb s_crumb;

// The first checkpoint of a boot overwrites the one the last run left behind, so the old
// one is copied out first.
bool s_captured = false;
char s_previous_label[crash_breadcrumb::kLabelMax] = "";
uint32_t s_previous_ms = 0;

void captureOnce() {
  if (s_captured) return;
  s_captured = true;
  if (crash_breadcrumb::copyLabel(s_crumb, s_previous_label, sizeof(s_previous_label)) > 0) {
    s_previous_ms = s_crumb.uptimeMs;
  }
}

const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt_watchdog";
    case ESP_RST_TASK_WDT: return "task_watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_EXT: return "external";
    default: return "unknown";
  }
}

}  // namespace

void checkpoint(const char* label) {
  captureOnce();
  crash_breadcrumb::set(s_crumb, label, millis());
}

size_t reportJson(char* out, size_t cap) {
  captureOnce();
  crash_breadcrumb::Report r{};
  r.resetReason = resetReasonName(esp_reset_reason());
  r.checkpoint = s_previous_label;
  r.checkpointUptimeMs = s_previous_ms;
  r.runningBuild = config::kFirmwareBuild;
  r.pendingBuild = services::ota::pendingBuild();
  r.blacklistCount = services::ota::blacklistedBuilds(r.blacklist);
  r.heapFree = ESP.getFreeHeap();
  r.heapLowest = ESP.getMinFreeHeap();
  r.slot = services::ota::runningSlotLabel();
  return crash_breadcrumb::formatReportJson(r, out, cap);
}

}  // namespace services::crash_log

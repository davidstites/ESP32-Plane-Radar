#include "services/ota_service.h"

#include <Arduino.h>
#include <Ed25519.h>
#include <atomic>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>

#include "config.h"
#include "hardware/ota_platform.h"
#include "ota_core/keys.h"
#include "ota_core/signature.h"
#include "ota_core/wdt_gate.h"

// Arduino marks the running app valid before setup() unless this returns true. We confirm
// only after a health check (confirmTick), so a bad build falls back to the old slot.
extern "C" bool verifyRollbackLater() { return true; }

#define PLANE_RADAR_STR_(x) #x
#define PLANE_RADAR_STR(x) PLANE_RADAR_STR_(x)

namespace services::ota {

namespace {

// ota_release.py sign reads this out of the image so a release is never signed under a build
// number the image does not report. init() prints it, which keeps the linker from dropping it.
const char kBuildMarker[] = "PLANERADAR-BUILD=" PLANE_RADAR_STR(PLANE_RADAR_BUILD);

bool ed25519Verify(const uint8_t* sig, const uint8_t* pub, const uint8_t* msg, size_t len) {
  return Ed25519::verify(sig, pub, msg, len);
}

hardware::PrefsStore s_store;
hardware::EspSlots s_slots;
hardware::TlsHttp s_http;
ota_core::Updater* s_updater = nullptr;  // heap: it carries ~4 KB of buffers
std::atomic<bool> s_pending_verify{false};
std::atomic<bool> s_check_running{false};
ota_core::LoopWdtGate s_wdt_gate;

}  // namespace

void init() {
  s_store.begin();  // NVS is up by setup(); a static constructor would run before it
  ota_core::Config cfg{};
  cfg.basePath = config::kOtaBasePath;
  uint8_t mac[6];
  hardware::factoryMac(mac);
  ota_core::computeDeviceId(mac, cfg.deviceId);
  cfg.runningBuild = config::kFirmwareBuild;
  cfg.keys = ota_core::keys::keyList();
  cfg.verify = ed25519Verify;
  s_updater = new ota_core::Updater(cfg, s_store, s_slots, s_http);

  char hash[33];
  ota_core::deviceHashHex(cfg.deviceId, hash);
  const ota_core::BootInfo info = s_updater->onBoot();
  s_pending_verify.store(info.pendingVerify);
  Serial.printf("ota: build %u, device %s, slot %s, store %s\n", static_cast<unsigned>(cfg.runningBuild),
                hash, runningSlotLabel(), s_store.ok() ? "ok" : "UNAVAILABLE");
  Serial.printf("ota: %s\n", kBuildMarker);
  if (info.rolledBack) {
    Serial.printf("ota: build %u failed and was rolled back; blacklisted\n",
                  static_cast<unsigned>(info.rolledBackBuild));
  }
  if (s_pending_verify.load()) {
    Serial.println("ota: new build awaiting confirmation");
  }
}

void recoveryWindow() {
  if (s_updater == nullptr) {
    return;
  }
  pinMode(config::kBootPin, INPUT_PULLUP);
  bool held = false;
  unsigned long heldSince = 0;
  while (held || millis() < config::kOtaRecoveryWindowMs) {
    if (digitalRead(config::kBootPin) == LOW) {
      if (!held) {
        held = true;
        heldSince = millis();
      } else if (millis() - heldSince >= config::kOtaRecoveryHoldMs) {
        Serial.println("ota: recovery button held; returning to the previous firmware");
        if (!s_updater->recover()) {
          Serial.println("ota: no previous firmware to return to");
        }
        return;  // only reached when recovery was not possible
      }
    } else {
      held = false;
    }
    delay(10);
  }
}

bool checkRunning() { return s_check_running.load(); }

ota_core::CheckResult checkNow() {
  if (s_updater == nullptr) {
    return ota_core::CheckResult{ota_core::Outcome::kFailed, "store_unavailable", 0, false};
  }
  Serial.printf("ota: check starting; heap free %u, largest block %u\n", static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
  s_check_running.store(true);
  const ota_core::CheckResult res = s_updater->check();
  s_check_running.store(false);
  Serial.printf("ota: check -> %s (build %u%s); heap free %u (lowest ever %u), largest block %u, stack headroom %u B\n",
                res.reason, static_cast<unsigned>(res.build), res.candidate ? ", candidate" : "",
                static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMinFreeHeap()),
                static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
                static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  return res;
}

void confirmTick() {
  if (!s_pending_verify.load() || s_updater == nullptr) {
    return;
  }
  if (s_wdt_gate.arm()) {
    // Armed from loop(), not init(): setup() can legitimately block for tens of seconds on
    // Wi-Fi, and the loop watchdog is only fed between loop() calls.
    enableLoopWDT();  // a hung loop() resets, and the bootloader then rolls back
  }
  // Confirmation is local only: the build started and kept running. The network is never a
  // health criterion, so being offline before, during or after an update never rolls back.
  if (millis() >= config::kOtaConfirmAfterMs && s_updater->confirmBoot()) {
    s_pending_verify.store(false);
    s_wdt_gate.disarm();
    disableLoopWDT();
    Serial.println("ota: new build confirmed");
  }
}

void feedLoopWdtIfArmed() {
  if (s_wdt_gate.shouldFeed()) {
    feedLoopWDT();
  }
}

uint32_t pendingBuild() { return ota_core::OtaState(s_store).pending(); }

size_t blacklistedBuilds(uint32_t out[8]) { return ota_core::OtaState(s_store).blacklistEntries(out); }

const char* runningSlotLabel() {
  const esp_partition_t* p = esp_ota_get_running_partition();
  return p != nullptr ? p->label : "?";
}

}  // namespace services::ota

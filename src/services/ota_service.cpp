#include "services/ota_service.h"

#include <Arduino.h>
#include <Ed25519.h>
#include <WiFi.h>
#include <esp_ota_ops.h>

#include "config.h"
#include "hardware/ota_platform.h"
#include "ota_core/keys.h"
#include "ota_core/signature.h"
#include "services/adsb_client.h"

// Arduino marks the running app valid before setup() unless this returns true. We confirm
// only after a health check (confirmTick), so a bad build falls back to the old slot.
extern "C" bool verifyRollbackLater() { return true; }

namespace services::ota {

namespace {

bool ed25519Verify(const uint8_t* sig, const uint8_t* pub, const uint8_t* msg, size_t len) {
  return Ed25519::verify(sig, pub, msg, len);
}

hardware::PrefsStore s_store;
hardware::EspSlots s_slots;
hardware::TlsHttp s_http;
ota_core::Updater* s_updater = nullptr;  // heap: it carries ~4 KB of buffers
bool s_pending_verify = false;

bool adsbHasSucceeded() {
  unsigned long last_update_ms = 0;
  services::adsb::Aircraft unused;
  services::adsb::snapshotAircraft(&unused, 0, &last_update_ms);
  return last_update_ms != 0;
}

}  // namespace

void init() {
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
  s_pending_verify = info.pendingVerify;
  Serial.printf("ota: build %u, device %s, slot %s, store %s\n", static_cast<unsigned>(cfg.runningBuild),
                hash, runningSlotLabel(), s_store.ok() ? "ok" : "UNAVAILABLE");
  if (info.rolledBack) {
    Serial.printf("ota: build %u failed and was rolled back; blacklisted\n",
                  static_cast<unsigned>(info.rolledBackBuild));
  }
  if (s_pending_verify) {
    Serial.println("ota: new build awaiting confirmation");
    enableLoopWDT();  // a hung loop() resets, and the bootloader then rolls back
  }
}

ota_core::CheckResult checkNow() {
  if (s_updater == nullptr) {
    return ota_core::CheckResult{ota_core::Outcome::kFailed, "store_unavailable", 0, false};
  }
  const ota_core::CheckResult res = s_updater->check();
  Serial.printf("ota: check -> %s (build %u%s)\n", res.reason, static_cast<unsigned>(res.build),
                res.candidate ? ", candidate" : "");
  return res;
}

void confirmTick() {
  if (!s_pending_verify || s_updater == nullptr) {
    return;
  }
  if (WiFi.status() == WL_CONNECTED && adsbHasSucceeded()) {
    if (s_updater->confirmBoot()) {
      s_pending_verify = false;
      disableLoopWDT();
      Serial.println("ota: new build confirmed");
    }
    return;
  }
  if (millis() >= config::kOtaConfirmDeadlineMs) {
    Serial.println("ota: new build never became healthy; restarting into the old one");
    esp_restart();
  }
}

uint32_t pendingBuild() { return ota_core::OtaState(s_store).pending(); }

size_t blacklistedBuilds(uint32_t out[8]) { return ota_core::OtaState(s_store).blacklistEntries(out); }

const char* runningSlotLabel() {
  const esp_partition_t* p = esp_ota_get_running_partition();
  return p != nullptr ? p->label : "?";
}

}  // namespace services::ota

#pragma once

#include <Preferences.h>

#include <cstdint>
#include <memory>

#include "ota_core/hal.h"

class WiFiClientSecure;
class HTTPClient;

namespace hardware {

/** NVS-backed OTA state ("otacore" namespace). ok() is whether the namespace opened. */
class PrefsStore : public ota_core::IOtaStore {
 public:
  PrefsStore();
  bool ok() const override { return ok_; }
  uint32_t getUInt(const char* key, uint32_t def) override;
  bool putUInt(const char* key, uint32_t v) override;
  size_t getBytes(const char* key, void* buf, size_t cap) override;
  bool putBytes(const char* key, const void* buf, size_t len) override;
  bool erase(const char* key) override;

 private:
  Preferences prefs_;
  bool ok_;
};

/** The two app slots, via esp_ota_* and the Arduino Update class. */
class EspSlots : public ota_core::IOtaSlots {
 public:
  ota_core::SlotState runningState() override;
  bool hasInvalidSlot() override;
  bool markRunningValid() override;
  bool beginUpdate(size_t size) override;
  size_t writeUpdate(const uint8_t* data, size_t len) override;
  bool endUpdate() override;
  void abortUpdate() override;
};

/** HTTPS to config::kOtaHost, verified against the embedded CA bundle, no redirects. */
class TlsHttp : public ota_core::IOtaHttp {
 public:
  TlsHttp();
  ~TlsHttp() override;
  bool fetchManifest(const char* path, const char* ifNoneMatch, ota_core::ManifestResponse& out) override;
  int64_t openImage(const char* path) override;
  int readImage(uint8_t* buf, size_t cap) override;
  void closeImage() override;

 private:
  bool begin(const char* path);
  void end();

  std::unique_ptr<WiFiClientSecure> client_;
  std::unique_ptr<HTTPClient> http_;
};

/** The factory-programmed base MAC, the identity the device id is derived from. */
void factoryMac(uint8_t out[6]);

}  // namespace hardware

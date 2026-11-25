#include "hardware/ota_platform.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <esp_mac.h>
#include <esp_ota_ops.h>

#include <cstring>

#include "config.h"

extern const uint8_t _binary_data_x509_crt_bundle_bin_start[] asm(
    "_binary_data_x509_crt_bundle_bin_start");

namespace hardware {

namespace {
constexpr char kNamespace[] = "otacore";
constexpr uint32_t kIdleTimeoutMs = 15000;
}  // namespace

PrefsStore::PrefsStore() : ok_(prefs_.begin(kNamespace, false)) {}

uint32_t PrefsStore::getUInt(const char* key, uint32_t def) { return ok_ ? prefs_.getUInt(key, def) : def; }

bool PrefsStore::putUInt(const char* key, uint32_t v) { return ok_ && prefs_.putUInt(key, v) == sizeof(uint32_t); }

size_t PrefsStore::getBytes(const char* key, void* buf, size_t cap) {
  if (!ok_) return 0;
  const size_t len = prefs_.getBytesLength(key);
  if (len == 0 || len > cap) return 0;
  return prefs_.getBytes(key, buf, len);
}

bool PrefsStore::putBytes(const char* key, const void* buf, size_t len) {
  return ok_ && prefs_.putBytes(key, buf, len) == len;
}

bool PrefsStore::erase(const char* key) {
  if (!ok_) return false;
  return !prefs_.isKey(key) || prefs_.remove(key);  // absent already counts as erased
}

ota_core::SlotState EspSlots::runningState() {
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) != ESP_OK) {
    return ota_core::SlotState::kUndefined;
  }
  switch (state) {
    case ESP_OTA_IMG_NEW: return ota_core::SlotState::kNew;
    case ESP_OTA_IMG_PENDING_VERIFY: return ota_core::SlotState::kPendingVerify;
    case ESP_OTA_IMG_VALID: return ota_core::SlotState::kValid;
    case ESP_OTA_IMG_INVALID: return ota_core::SlotState::kInvalid;
    case ESP_OTA_IMG_ABORTED: return ota_core::SlotState::kAborted;
    default: return ota_core::SlotState::kUndefined;
  }
}

bool EspSlots::hasInvalidSlot() { return esp_ota_get_last_invalid_partition() != nullptr; }

bool EspSlots::markRunningValid() { return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK; }

bool EspSlots::beginUpdate(size_t size) { return Update.begin(size); }

size_t EspSlots::writeUpdate(const uint8_t* data, size_t len) {
  return Update.write(const_cast<uint8_t*>(data), len);
}

bool EspSlots::endUpdate() { return Update.end() && Update.isFinished(); }

void EspSlots::abortUpdate() { Update.abort(); }

TlsHttp::TlsHttp() = default;
TlsHttp::~TlsHttp() = default;

// A fresh client per request keeps the ~56 KB TLS session's heap free between checks.
bool TlsHttp::begin(const char* path) {
  end();
  client_.reset(new WiFiClientSecure());
  client_->setCACertBundle(_binary_data_x509_crt_bundle_bin_start);
  http_.reset(new HTTPClient());
  http_->setReuse(false);
  http_->setTimeout(kIdleTimeoutMs);
  http_->setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  const char* headers[] = {"ETag"};
  http_->collectHeaders(headers, 1);
  String url = "https://";
  url += config::kOtaHost;
  url += path;
  return http_->begin(*client_, url);
}

void TlsHttp::end() {
  if (http_) http_->end();
  http_.reset();
  if (client_) client_->stop();
  client_.reset();
}

bool TlsHttp::fetchManifest(const char* path, const char* ifNoneMatch, ota_core::ManifestResponse& out) {
  memset(&out, 0, sizeof(out));
  if (!begin(path)) {
    end();
    return false;
  }
  if (ifNoneMatch != nullptr && ifNoneMatch[0] != '\0') http_->addHeader("If-None-Match", ifNoneMatch);
  const int code = http_->GET();
  if (code <= 0) {
    end();
    return false;
  }
  out.status = code;
  const String etag = http_->header("ETag");
  if (etag.length() <= sizeof(out.etag) - 1) strcpy(out.etag, etag.c_str());

  if (code == 200) {
    WiFiClient* stream = http_->getStreamPtr();
    uint32_t last = millis();
    while (out.bodyLen <= ota_core::kMaxManifestBytes && millis() - last < kIdleTimeoutMs) {
      if (stream->available()) {
        out.body[out.bodyLen++] = static_cast<char>(stream->read());
        last = millis();
      } else if (!stream->connected()) {
        break;
      } else {
        delay(5);
      }
    }
    if (out.bodyLen > ota_core::kMaxManifestBytes) {
      out.tooLarge = true;
      out.bodyLen = ota_core::kMaxManifestBytes;
    }
    out.body[out.bodyLen] = '\0';
  }
  end();
  return true;
}

int64_t TlsHttp::openImage(const char* path) {
  if (!begin(path)) {
    end();
    return -1;
  }
  const int code = http_->GET();
  if (code <= 0) {
    end();
    return -1;
  }
  if (code != 200) {
    end();
    return -code;
  }
  const int size = http_->getSize();
  if (size < 0) {  // chunked: no length to size the slot against
    end();
    return -1;
  }
  return size;
}

int TlsHttp::readImage(uint8_t* buf, size_t cap) {
  if (!http_) return -1;
  WiFiClient* stream = http_->getStreamPtr();
  const uint32_t start = millis();
  while (!stream->available()) {
    if (!stream->connected()) return 0;
    if (millis() - start > kIdleTimeoutMs) return -1;
    delay(5);
  }
  const size_t want = cap < static_cast<size_t>(stream->available()) ? cap : stream->available();
  return static_cast<int>(stream->readBytes(buf, want));
}

void TlsHttp::closeImage() { end(); }

void factoryMac(uint8_t out[6]) { esp_efuse_mac_get_default(out); }

}  // namespace hardware

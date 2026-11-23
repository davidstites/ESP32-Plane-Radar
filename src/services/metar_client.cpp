#include "services/metar_client.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstring>

#include "services/http_body.h"

extern const uint8_t _binary_data_x509_crt_bundle_bin_start[] asm(
    "_binary_data_x509_crt_bundle_bin_start");

namespace services::metar {

namespace {

using metar_parse::Category;
using metar_parse::kMaxStations;
using metar_parse::Station;

constexpr char kHost[] = "https://aviationweather.gov";
constexpr uint32_t kRequestTimeoutMs = 10000;
/** Reports are at most hours old by design; if refreshes keep failing, stop showing them. */
constexpr unsigned long kMaxDataAgeMs = 3UL * 60UL * 60UL * 1000UL;
constexpr int kHttpNoContent = 204;

// Written by the network task, read by the render loop; s_mutex guards all three.
Station s_stations[kMaxStations];
size_t s_count = 0;
unsigned long s_published_ms = 0;
bool s_has_data = false;
SemaphoreHandle_t s_mutex = nullptr;

void publish(const Station* stations, size_t count) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  for (size_t i = 0; i < count; ++i) {
    s_stations[i] = stations[i];
  }
  s_count = count;
  s_published_ms = millis();
  s_has_data = true;
  xSemaphoreGive(s_mutex);
}

}  // namespace

void init() {
  if (s_mutex == nullptr) {
    s_mutex = xSemaphoreCreateMutex();
  }
}

bool fetch(const char* const* icaos, size_t count) {
  char path[256];
  if (!metar_parse::buildRequestPath(path, sizeof(path), icaos, count)) {
    Serial.println("metar: could not build request");
    return false;
  }
  String url = kHost;
  url += path;

  WiFiClientSecure client;
  client.setCACertBundle(_binary_data_x509_crt_bundle_bin_start);
  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(kRequestTimeoutMs);
  services::http::collectBodyHeaders(http);
  if (!http.begin(client, url)) {
    Serial.println("metar: http.begin failed");
    return false;
  }

  const int code = http.GET();
  if (code == kHttpNoContent) {  // no airport in the request has a recent report
    publish(nullptr, 0);
    http.end();
    return true;
  }
  if (code != HTTP_CODE_OK) {
    char tls_error[80] = "";
    client.lastError(tls_error, sizeof(tls_error));
    Serial.printf("metar: HTTP %d (%s)\n", code, tls_error);
    http.end();
    return false;
  }

  Station parsed[kMaxStations];
  adsb_parse::BodyReader body = services::http::bodyReader(http, client, kRequestTimeoutMs);
  const metar_parse::ParseResult result = metar_parse::parseStations(body, parsed, kMaxStations);
  http.end();
  if (!result.ok) {
    Serial.printf("metar: %s\n", result.error);
    return false;
  }
  publish(parsed, result.count);
  Serial.printf("metar: %u of %u airports have a report\n", static_cast<unsigned>(result.count),
                static_cast<unsigned>(count));
  return true;
}

Category category(const char* icao) {
  Category found = Category::kUnknown;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (s_has_data && millis() - s_published_ms < kMaxDataAgeMs) {
    for (size_t i = 0; i < s_count; ++i) {
      if (strcmp(s_stations[i].icao, icao) == 0) {
        found = s_stations[i].category;
        break;
      }
    }
  }
  xSemaphoreGive(s_mutex);
  return found;
}

}  // namespace services::metar

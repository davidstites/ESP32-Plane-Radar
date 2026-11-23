#include "services/precip_client.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <lgfx/utility/lgfx_miniz.h>
#include <nexrad_decode/radar_image.h>

#include <cstdlib>

#include "services/http_body.h"
#include "ui/radar_theme.h"

namespace services::precip {

namespace {

// Plain HTTP: the radar host's five-certificate TLS chain does not fit in this heap, and the
// data is public imagery that is only ever decoded by the bounded decoder below.
constexpr char kHost[] = "http://mesonet.agron.iastate.edu";
constexpr uint32_t kRequestTimeoutMs = 10000;
/** The image is ~0.1-6 KB; anything that does not fit is not the image we asked for. */
constexpr size_t kPngCapacity = 12 * 1024;
/** The image reaches this many screen pixels from the center on each side. */
constexpr int kHalfExtentPx = ui::radar::kSize / 2;

// Written by the network task, read by the render loop; s_mutex guards all three.
nexrad::Grid s_grid;
radar_geo::ViewKey s_view = {};
bool s_has_grid = false;
SemaphoreHandle_t s_mutex = nullptr;

bool minizInflate(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_len) {
  return lgfx_tinfl_decompress_mem_to_mem(out, out_len, in, in_len, TINFL_FLAG_PARSE_ZLIB_HEADER) ==
         out_len;
}

/** Download the response body into png; returns its length, or 0 on any failure. */
size_t download(const String& url, uint8_t* png) {
  WiFiClient client;
  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(kRequestTimeoutMs);
  services::http::collectBodyHeaders(http);
  if (!http.begin(client, url)) {
    Serial.println("precip: http.begin failed");
    return 0;
  }
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("precip: HTTP %d\n", code);
    http.end();
    return 0;
  }

  adsb_parse::BodyReader body = services::http::bodyReader(http, client, kRequestTimeoutMs);
  size_t got = 0;
  while (got < kPngCapacity) {
    const size_t n = body.readBytes(reinterpret_cast<char*>(png) + got, kPngCapacity - got);
    if (n == 0) {
      break;
    }
    got += n;
  }
  const bool complete = body.status() == adsb_parse::BodyStatus::kComplete;
  http.end();
  if (!complete) {
    Serial.println("precip: image did not arrive in full");
    return 0;
  }
  return got;
}

}  // namespace

void init() {
  if (s_mutex == nullptr) {
    s_mutex = xSemaphoreCreateMutex();
  }
}

bool fetch(const radar_geo::ViewKey& view, float outer_km) {
  const nexrad::Bounds bounds = nexrad::viewBounds(view.center_lat, view.center_lon, outer_km,
                                                   ui::radar::kGridOuterRadius, kHalfExtentPx);
  char path[512];
  if (!nexrad::buildRequestPath(path, sizeof(path), bounds)) {
    Serial.println("precip: could not build request");
    return false;
  }
  String url = kHost;
  url += path;

  // Both buffers up front, while the heap is still in large pieces.
  uint8_t* scratch = static_cast<uint8_t*>(malloc(nexrad::kScratchBytes));
  uint8_t* png = static_cast<uint8_t*>(malloc(kPngCapacity));
  bool ok = false;
  if (scratch == nullptr || png == nullptr) {
    Serial.println("precip: out of memory for the image");
  } else {
    const size_t length = download(url, png);
    if (length > 0) {
      xSemaphoreTake(s_mutex, portMAX_DELAY);
      const nexrad::DecodeResult result =
          nexrad::decodePalettePng(png, length, scratch, nexrad::kScratchBytes, minizInflate,
                                   &s_grid);
      ok = result == nexrad::DecodeResult::kOk;
      if (ok) {
        // The panel is round: cells beyond the outer ring are never drawn.
        s_grid.maskOutsideDisc(ui::radar::kGridOuterRadius / 2);
        s_view = view;
        s_has_grid = true;
      }
      xSemaphoreGive(s_mutex);
      if (ok) {
        Serial.printf("precip: %u bytes; light %u, moderate %u, heavy %u cells\n",
                      static_cast<unsigned>(length),
                      static_cast<unsigned>(s_grid.count(nexrad::Level::kLight)),
                      static_cast<unsigned>(s_grid.count(nexrad::Level::kModerate)),
                      static_cast<unsigned>(s_grid.count(nexrad::Level::kHeavy)));
      } else {
        Serial.printf("precip: %s\n", nexrad::describe(result));
      }
    }
  }
  free(scratch);
  free(png);
  return ok;
}

const nexrad::Grid* lock(radar_geo::ViewKey* view) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (!s_has_grid) {
    return nullptr;
  }
  *view = s_view;
  return &s_grid;
}

void unlock() { xSemaphoreGive(s_mutex); }

}  // namespace services::precip

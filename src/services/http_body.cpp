#include "services/http_body.h"

#include <Arduino.h>

namespace services::http {

namespace {

adsb_parse::BodyFraming framingOf(HTTPClient& http) {
  if (http.header("Transfer-Encoding").equalsIgnoreCase("chunked")) {
    return adsb_parse::BodyFraming::kChunked;
  }
  if (http.getSize() >= 0) {
    return adsb_parse::BodyFraming::kContentLength;
  }
  return adsb_parse::BodyFraming::kUntilClose;
}

}  // namespace

void collectBodyHeaders(HTTPClient& http) {
  static const char* kHeaders[] = {"Transfer-Encoding"};
  http.collectHeaders(kHeaders, 1);
}

adsb_parse::BodyReader bodyReader(HTTPClient& http, WiFiClient& client, uint32_t timeout_ms) {
  // Reads what the socket already holds; BodyReader idles between calls.
  const auto source = [&client](uint8_t* buf, size_t len) -> int {
    const int available = client.available();
    if (available > 0) {
      const size_t want = static_cast<size_t>(available) < len ? static_cast<size_t>(available) : len;
      const int n = client.read(buf, want);
      return n < 0 ? -1 : n;
    }
    if (available < 0) {
      return -1;
    }
    return client.connected() ? 0 : -1;
  };
  return adsb_parse::BodyReader(
      source, framingOf(http), http.getSize(),
      [] { return static_cast<uint32_t>(millis()); }, [] { delay(1); }, timeout_ms);
}

}  // namespace services::http

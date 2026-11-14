#include "services/adsb_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "adsb_parse/body_reader.h"
#include "adsb_parse/retry_policy.h"
#include "config.h"

extern const uint8_t _binary_data_x509_crt_bundle_bin_start[] asm(
    "_binary_data_x509_crt_bundle_bin_start");

namespace services::adsb {

namespace {

constexpr char kApiBase[] = "https://opendata.adsb.fi/api/v3/lat/";
constexpr float kKmPerNm = 1.852f;
constexpr int kConnectAttemptMs = 200;
constexpr unsigned long kRequestTimeoutMs = 10000;

Aircraft s_aircraft[kMaxAircraft];
size_t s_aircraft_count = 0;
PollFn s_poll_fn = nullptr;

// One TLS client and HTTP client for the life of the program: with keep-alive the
// TLS session (and its ~40 KB of mbedTLS buffers) survives between fetches.
WiFiClientSecure s_client;
HTTPClient s_http;
bool s_http_configured = false;

void configureHttpOnce() {
  if (s_http_configured) {
    return;
  }
  static const char* kCollectedHeaders[] = {"Transfer-Encoding"};
  s_client.setCACertBundle(_binary_data_x509_crt_bundle_bin_start);
  s_http.setReuse(true);
  s_http.collectHeaders(kCollectedHeaders, 1);
  s_http_configured = true;
}

void pollNetwork() {
  if (s_poll_fn != nullptr) {
    s_poll_fn();
  }
}

int performGetWithPoll(HTTPClient& http) {
  http.setConnectTimeout(kConnectAttemptMs);
  const unsigned long deadline = millis() + kRequestTimeoutMs;
  while (millis() < deadline) {
    pollNetwork();
    const int code = http.GET();
    if (code > 0) {
      return code;
    }
    if (code != HTTPC_ERROR_CONNECTION_REFUSED &&
        code != HTTPC_ERROR_NOT_CONNECTED) {
      return code;
    }
    delay(5);
  }
  return HTTPC_ERROR_READ_TIMEOUT;
}

adsb_parse::BodyFraming responseFraming(HTTPClient& http) {
  if (http.header("Transfer-Encoding").equalsIgnoreCase("chunked")) {
    return adsb_parse::BodyFraming::kChunked;
  }
  if (http.getSize() >= 0) {
    return adsb_parse::BodyFraming::kContentLength;
  }
  return adsb_parse::BodyFraming::kUntilClose;
}

/** Reads what the socket already has; never blocks (BodyReader idles between calls). */
int readAvailable(WiFiClient* stream, uint8_t* buf, size_t len) {
  const int available = stream->available();
  if (available > 0) {
    const size_t want = static_cast<size_t>(available) < len ? available : len;
    const int n = stream->read(buf, want);
    return n < 0 ? -1 : n;
  }
  return stream->connected() ? 0 : -1;
}

float kmToNauticalMiles(float km) { return km / kKmPerNm; }

}  // namespace

void setPollFn(PollFn fn) { s_poll_fn = fn; }

size_t aircraftCount() { return s_aircraft_count; }

const Aircraft* aircraftList() { return s_aircraft; }

bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km) {
  const float dist_nm = kmToNauticalMiles(fetch_radius_km);

  String url = kApiBase;
  url += String(center_lat, 6);
  url += "/lon/";
  url += String(center_lon, 6);
  url += "/dist/";
  url += String(dist_nm, 1);

  configureHttpOnce();
  const bool reused = s_client.connected();
  if (!s_http.begin(s_client, url)) {
    Serial.println("adsb: http.begin failed");
    return false;
  }

  s_http.setTimeout(kRequestTimeoutMs);
  int code = performGetWithPoll(s_http);
  if (adsb_parse::shouldRetryOnFreshConnection(code, reused)) {
    Serial.printf("adsb: kept-alive connection failed (%d), reconnecting\n", code);
    s_client.stop();
    code = performGetWithPoll(s_http);
  }
  if (code != HTTP_CODE_OK) {
    Serial.printf("adsb: HTTP %d\n", code);
    s_client.stop();
    s_http.end();
    return false;
  }

  WiFiClient* stream = s_http.getStreamPtr();
  if (stream == nullptr) {
    Serial.println("adsb: connection lost before body");
    s_client.stop();
    s_http.end();
    return false;
  }

  adsb_parse::BodyReader body(
      [stream](uint8_t* buf, size_t len) { return readAvailable(stream, buf, len); },
      responseFraming(s_http), s_http.getSize(),
      [] { return static_cast<uint32_t>(millis()); },
      [] {
        pollNetwork();
        delay(1);
      },
      kRequestTimeoutMs);

  const adsb_parse::ParseResult result = adsb_parse::parseAircraft(
      body, config::kAdsbShowGroundAircraft, s_aircraft, kMaxAircraft);
  if (!body.reusable()) {
    s_client.stop();
  }
  s_http.end();

  if (!result.ok) {
    Serial.printf("adsb: parse error: %s (body status %d)\n", result.error,
                  static_cast<int>(body.status()));
    return false;
  }

  s_aircraft_count = result.count;
  Serial.printf("adsb: %u aircraft\n", static_cast<unsigned>(result.count));
  return true;
}

}  // namespace services::adsb

#pragma once

#include <ArduinoJson.h>

#include <cstddef>

namespace adsb_parse {

class BodyReader;

struct Aircraft {
  float lat;
  float lon;
  float nose_deg;
  float track_deg;
  float gs_knots;
  char callsign[9];
  char type[5];
  char alt[12];
};

constexpr size_t kMaxAircraft = 64;

/** Outcome of a parse. error is nullptr on success, else a static string. */
struct ParseResult {
  bool ok;
  size_t count;
  const char* error;
};

/** Fill filter with the adsb.fi "ac" keys the firmware reads; all others are dropped. */
void buildAircraftFilter(JsonDocument& filter);

/**
 * Parse an adsb.fi v3 response held in memory into out (up to capacity entries).
 * Fails unless the JSON parses and has an "ac" array; out is only written on success.
 */
ParseResult parseAircraft(const char* json, size_t len, bool include_ground, Aircraft* out,
                          size_t capacity);

/**
 * Parse an adsb.fi v3 response straight from an HTTP body, then drain the body so
 * the connection can be reused. Same success rule as the in-memory overload.
 */
ParseResult parseAircraft(BodyReader& body, bool include_ground, Aircraft* out,
                          size_t capacity);

}  // namespace adsb_parse

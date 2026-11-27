#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>

namespace adsb_parse {

class BodyReader;

struct Aircraft {
  float lat;
  float lon;
  float nose_deg;
  float track_deg;
  /** Ground speed in whole knots (gs, else tas, else ias), for dead reckoning and the vector. */
  uint16_t gs_knots;
  /** Age of the position fix when fetched (ms), from the feed's seen_pos; capped at kMaxPosAgeMs. */
  uint16_t pos_age_ms;
  /** Whole knots shown in the tag; 0 when on the ground or gs is absent or rounds to zero. */
  uint16_t speed_kts;
  char callsign[9];
  char type[5];
  char alt[10];
};

constexpr size_t kMaxAircraft = 64;
/** A staler fix is not extrapolated further, so it isn't flung across the screen. */
constexpr uint16_t kMaxPosAgeMs = 30000;

/** Outcome of a parse. error is nullptr on success, else a static string. */
struct ParseResult {
  bool ok;
  size_t count;
  const char* error;
};

/** Tag text for the speed line ("450 kts"); empty when speed_kts is 0. */
void formatSpeed(const Aircraft& ac, char* out, size_t out_len);

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

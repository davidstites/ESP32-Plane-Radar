#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>

namespace metar_parse {

/** NOAA flight category, as aviationweather.gov reports it in "fltCat". */
enum class Category : uint8_t { kUnknown, kVfr, kMvfr, kIfr, kLifr };

struct Station {
  char icao[5];
  Category category;
};

/** Most airports one request covers: the on-screen label cap. */
constexpr size_t kMaxStations = 32;

/** Outcome of a parse. error is nullptr on success, else a static string. */
struct ParseResult {
  bool ok;
  size_t count;
  const char* error;
};

/** "VFR", "MVFR", "IFR" or "LIFR"; anything else, including nullptr, is kUnknown. */
Category categoryFromText(const char* text);

/** Fill filter with the only two keys the firmware reads from each report. */
void buildMetarFilter(JsonDocument& filter);

/**
 * Fill out (up to capacity) from a parsed, filtered response. The feed lists a station's
 * newest report first, so the first entry per station wins and later ones are skipped.
 * Fails unless doc is a JSON array. Entries without a usable 4-character id are skipped.
 */
ParseResult extractStations(const JsonDocument& doc, Station* out, size_t capacity);

/**
 * Parse a response body (anything ArduinoJson can read: a string, or a stream such as the
 * HTTP body reader) into out. out is only written on success.
 */
template <typename Input>
ParseResult parseStations(Input&& input, Station* out, size_t capacity) {
  JsonDocument filter;
  buildMetarFilter(filter);
  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, input, DeserializationOption::Filter(filter));
  if (err) {
    return {false, 0, "METAR JSON did not parse"};
  }
  return extractStations(doc, out, capacity);
}

/**
 * Write "/api/data/metar?ids=A,B,...&format=json&hours=2" into out. Returns false, leaving
 * out empty, when there are no ids or the path does not fit.
 */
bool buildRequestPath(char* out, size_t out_size, const char* const* icaos, size_t count);

}  // namespace metar_parse

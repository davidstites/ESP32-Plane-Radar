#include "adsb_parse/aircraft_parser.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

#include "adsb_parse/body_reader.h"

namespace adsb_parse {

namespace {

constexpr const char* kFilterKeys[] = {
    "lat", "lon", "true_heading", "mag_heading", "track", "dir", "gs",
    "tas", "ias", "flight",       "hex",         "t",     "alt_baro", "alt_geom",
};

bool readJsonFloat(const JsonObject& obj, const char* key, float* out) {
  if (obj[key].is<float>() || obj[key].is<double>() || obj[key].is<int>()) {
    *out = obj[key].as<float>();
    return true;
  }
  return false;
}

float pickNoseHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "true_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "mag_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "track", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "dir", &v)) {
    return v;
  }
  return 0.0f;
}

float pickTrackHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "track", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "true_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "mag_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "dir", &v)) {
    return v;
  }
  return 0.0f;
}

float pickGroundSpeed(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "gs", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "tas", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "ias", &v)) {
    return v;
  }
  return 0.0f;
}

bool isOnGround(const JsonObject& plane) {
  if (!plane["alt_baro"].is<const char*>()) {
    return false;
  }
  return strcmp(plane["alt_baro"].as<const char*>(), "ground") == 0;
}

void copyJsonStringTrimmed(const JsonObject& obj, const char* key, char* out,
                           size_t out_len) {
  out[0] = '\0';
  if (!obj[key].is<const char*>()) {
    return;
  }
  const char* s = obj[key].as<const char*>();
  size_t n = strnlen(s, out_len - 1);
  while (n > 0 && s[n - 1] == ' ') {
    --n;
  }
  memcpy(out, s, n);
  out[n] = '\0';
}

void formatAltitudeTag(const JsonObject& plane, char* out, size_t out_len) {
  out[0] = '\0';
  if (isOnGround(plane)) {
    snprintf(out, out_len, "GND");
    return;
  }

  float alt = 0.0f;
  if (readJsonFloat(plane, "alt_baro", &alt) ||
      readJsonFloat(plane, "alt_geom", &alt)) {
    snprintf(out, out_len, "%d ft", static_cast<int>(std::lround(alt)));
  }
}

void fillTagFields(Aircraft* ac, const JsonObject& plane) {
  copyJsonStringTrimmed(plane, "flight", ac->callsign, sizeof(ac->callsign));
  if (ac->callsign[0] == '\0') {
    copyJsonStringTrimmed(plane, "hex", ac->callsign, sizeof(ac->callsign));
  }

  copyJsonStringTrimmed(plane, "t", ac->type, sizeof(ac->type));
  formatAltitudeTag(plane, ac->alt, sizeof(ac->alt));
}

size_t extractAircraft(JsonDocument& doc, bool include_ground, Aircraft* out,
                       size_t capacity) {
  size_t n = 0;
  for (JsonObject plane : doc["ac"].as<JsonArray>()) {
    if (n >= capacity) {
      break;
    }
    if (!plane["lat"].is<float>() || !plane["lon"].is<float>()) {
      continue;
    }
    if (isOnGround(plane) && !include_ground) {
      continue;
    }

    out[n].lat = plane["lat"].as<float>();
    out[n].lon = plane["lon"].as<float>();
    out[n].nose_deg = pickNoseHeading(plane);
    out[n].track_deg = pickTrackHeading(plane);
    out[n].gs_knots = pickGroundSpeed(plane);
    fillTagFields(&out[n], plane);
    ++n;
  }
  return n;
}

/** Filtered deserialize of input (ArduinoJson input args), then extract into out. */
template <typename... TInput>
ParseResult parseFiltered(bool include_ground, Aircraft* out, size_t capacity,
                          TInput&&... input) {
  JsonDocument filter;
  buildAircraftFilter(filter);
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, std::forward<TInput>(input)...,
                                                   DeserializationOption::Filter(filter));
  if (err) {
    return {false, 0, err.c_str()};
  }
  return {true, extractAircraft(doc, include_ground, out, capacity), nullptr};
}

}  // namespace

void buildAircraftFilter(JsonDocument& filter) {
  filter.clear();
  JsonObject plane = filter["ac"].add<JsonObject>();
  for (const char* key : kFilterKeys) {
    plane[key] = true;
  }
}

ParseResult parseAircraft(const char* json, size_t len, bool include_ground, Aircraft* out,
                          size_t capacity) {
  return parseFiltered(include_ground, out, capacity, json, len);
}

ParseResult parseAircraft(BodyReader& body, bool include_ground, Aircraft* out,
                          size_t capacity) {
  const ParseResult result = parseFiltered(include_ground, out, capacity, body);
  body.drain();
  return result;
}

}  // namespace adsb_parse

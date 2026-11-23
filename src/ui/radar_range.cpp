#include "ui/radar_range.h"

#include <radar_geo/radar_geo.h>

#include "ui/radar_theme.h"

#include <Preferences.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ui::radar {

namespace {

constexpr char kPrefsNamespace[] = "planeradar";
constexpr char kPrefsRangeKey[] = "rangeIdx";  // legacy: preset index
constexpr char kPrefsRingKmKey[] = "ring3Km";
constexpr char kPrefsMilesKey[] = "useMiles";
constexpr char kPrefsRunwaysKey[] = "showRwys";
constexpr uint8_t kDefaultRangeIndex = 1;  // 10 km ring
constexpr float kKmPerMile = radar_geo::kKmPerMile;

Preferences s_prefs;
float s_ring3_km = kRangePresets[kDefaultRangeIndex].ring3_km;
bool s_use_miles = false;
bool s_show_runways = true;

void saveRingKm() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putFloat(kPrefsRingKmKey, s_ring3_km);
  s_prefs.end();
}

void saveUseMiles() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putBool(kPrefsMilesKey, s_use_miles);
  s_prefs.end();
}

void saveShowRunways() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putBool(kPrefsRunwaysKey, s_show_runways);
  s_prefs.end();
}

bool portalCheckboxChecked(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  // WiFiManager checkbox submits its value= attribute ("T", or "F" if we prefilled F).
  if ((value[0] == 'T' || value[0] == 't' || value[0] == 'F' || value[0] == 'f') &&
      value[1] == '\0') {
    return true;
  }
  return strcmp(value, "on") == 0;
}

}  // namespace

void rangeInit() {
  if (!s_prefs.begin(kPrefsNamespace, true)) {
    return;
  }
  const float max_km = radar_geo::kMaxRingMiles * kKmPerMile;
  const float saved_km = s_prefs.getFloat(kPrefsRingKmKey, 0.0f);
  if (saved_km >= 1.0f && saved_km <= max_km) {
    s_ring3_km = saved_km;
  } else {
    const uint8_t saved = s_prefs.getUChar(kPrefsRangeKey, kDefaultRangeIndex);
    s_ring3_km = kRangePresets[(saved < kRangePresetCount) ? saved : kDefaultRangeIndex].ring3_km;
  }
  s_use_miles = s_prefs.getBool(kPrefsMilesKey, false);
  s_show_runways = s_prefs.getBool(kPrefsRunwaysKey, true);
  s_prefs.end();
}

void rangeNext() {
  float presets_km[kRangePresetCount];
  for (size_t i = 0; i < kRangePresetCount; ++i) {
    presets_km[i] = kRangePresets[i].ring3_km;
  }
  s_ring3_km = radar_geo::nextRingKm(s_ring3_km, presets_km, kRangePresetCount);
  saveRingKm();
}

RangePreset rangeCurrent() { return {s_ring3_km, s_ring3_km * kRing3ToOuterKm}; }

float fetchRadiusKm() {
  const float outer_km = rangeCurrent().outer_km;
  const float screen_r_px =
      static_cast<float>(kCenterX - kBeyondRingScreenMarginPx);
  return outer_km * (screen_r_px / static_cast<float>(kGridOuterRadius));
}

bool useMiles() { return s_use_miles; }

bool showRunways() { return s_show_runways; }

void saveMilesFromPortal(const char* checkbox_value) {
  s_use_miles = portalCheckboxChecked(checkbox_value);
  saveUseMiles();
  Serial.printf("Distance units: %s\n", s_use_miles ? "miles" : "km");
}

void saveRangeFromPortal(const char* text) {
  float km = 0.0f;
  if (!radar_geo::parseRingRangeKm(text, s_use_miles, &km)) {
    Serial.println("Invalid range in portal — keeping previous range");
    return;
  }
  s_ring3_km = km;
  saveRingKm();
  char label[12];
  formatCurrentRing3Label(label, sizeof(label));
  Serial.printf("Range: %s\n", label);
}

void formatRangeForPortal(char* buf, size_t len) {
  snprintf(buf, len, "%.1f", s_use_miles ? s_ring3_km / kKmPerMile : s_ring3_km);
}

void saveRunwaysFromPortal(const char* checkbox_value) {
  s_show_runways = portalCheckboxChecked(checkbox_value);
  saveShowRunways();
  Serial.printf("Runway overlay: %s\n", s_show_runways ? "on" : "off");
}

void formatRing3Label(char* buf, size_t len, float ring3_km, bool use_miles) {
  if (use_miles) {
    const int mi = static_cast<int>(lroundf(ring3_km / kKmPerMile));
    snprintf(buf, len, "%dmi", mi);
  } else {
    const int km = static_cast<int>(lroundf(ring3_km));
    snprintf(buf, len, "%dkm", km);
  }
}

void formatCurrentRing3Label(char* buf, size_t len) {
  formatRing3Label(buf, len, rangeCurrent().ring3_km, s_use_miles);
}

void unitsReset() {
  s_use_miles = false;
  s_show_runways = true;
  if (s_prefs.begin(kPrefsNamespace, false)) {
    s_prefs.remove(kPrefsMilesKey);
    s_prefs.remove(kPrefsRunwaysKey);
    s_prefs.end();
  }
}

}  // namespace ui::radar

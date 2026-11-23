#include "services/radar_location.h"

#include <Preferences.h>
#include <setup_link/setup_link.h>

#include "config.h"

namespace services::location {

namespace {

constexpr char kPrefsNamespace[] = "radar";
constexpr char kKeyLat[] = "lat";
constexpr char kKeyLon[] = "lon";

double s_lat = config::kDefaultRadarLat;
double s_lon = config::kDefaultRadarLon;
bool s_saved = false;

void persist(double lat, double lon) {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.putDouble(kKeyLat, lat);
  prefs.putDouble(kKeyLon, lon);
  prefs.end();
  s_lat = lat;
  s_lon = lon;
  s_saved = true;
}

}  // namespace

void init() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);
  if (prefs.isKey(kKeyLat) && prefs.isKey(kKeyLon)) {
    const double lat = prefs.getDouble(kKeyLat, config::kDefaultRadarLat);
    const double lon = prefs.getDouble(kKeyLon, config::kDefaultRadarLon);
    if (setup_link::validLatLon(lat, lon)) {
      s_lat = lat;
      s_lon = lon;
      s_saved = true;
    }
  }
  prefs.end();
}

double lat() { return s_lat; }

double lon() { return s_lon; }

bool hasSaved() { return s_saved; }

bool saveFromStrings(const char* lat_str, const char* lon_str) {
  double lat = 0.0;
  double lon = 0.0;
  if (!setup_link::parseLatLon(lat_str, lon_str, &lat, &lon)) {
    return false;
  }
  persist(lat, lon);
  Serial.printf("Radar location saved: %.6f, %.6f\n", lat, lon);
  return true;
}

void clear() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.remove(kKeyLat);
  prefs.remove(kKeyLon);
  prefs.end();
  s_lat = config::kDefaultRadarLat;
  s_lon = config::kDefaultRadarLon;
  s_saved = false;
}

}  // namespace services::location

#include "services/radar_location.h"

#include <Preferences.h>
#include <setup_link/setup_link.h>

#include <atomic>

#include "config.h"

namespace services::location {

namespace {

constexpr char kPrefsNamespace[] = "radar";
constexpr char kKeyLat[] = "lat";
constexpr char kKeyLon[] = "lon";
/** Set when the location was stored by this firmware's save path (see init()). */
constexpr char kKeyChosen[] = "chosen";

double s_lat = config::kDefaultRadarLat;
double s_lon = config::kDefaultRadarLon;
// Read by the network task, written by the loop task.
std::atomic<bool> s_saved{false};

/** Writes the pair to NVS; the in-memory location only changes when every write landed. */
bool persist(double lat, double lon) {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    return false;
  }
  const bool ok = prefs.putDouble(kKeyLat, lat) != 0 && prefs.putDouble(kKeyLon, lon) != 0 &&
                  prefs.putBool(kKeyChosen, true) != 0;
  prefs.end();
  if (!ok) {
    return false;
  }
  s_lat = lat;
  s_lon = lon;
  s_saved = true;
  return true;
}

}  // namespace

void init() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);
  if (prefs.isKey(kKeyLat) && prefs.isKey(kKeyLon)) {
    const double lat = prefs.getDouble(kKeyLat, config::kDefaultRadarLat);
    const double lon = prefs.getDouble(kKeyLon, config::kDefaultRadarLon);
    // Older firmware stored the prefilled default on every portal save, so exactly the
    // default pair without our marker was never a choice: ask for a location again.
    const bool legacy_default = !prefs.getBool(kKeyChosen, false) &&
                                lat == config::kDefaultRadarLat &&
                                lon == config::kDefaultRadarLon;
    if (setup_link::validLatLon(lat, lon) && !legacy_default) {
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

bool save(double lat, double lon) {
  if (!setup_link::validLatLon(lat, lon)) {
    return false;
  }
  if (!persist(lat, lon)) {
    Serial.println("Radar location could not be written to NVS");
    return false;
  }
  Serial.printf("Radar location saved: %.6f, %.6f\n", lat, lon);
  return true;
}

bool saveFromStrings(const char* lat_str, const char* lon_str) {
  double lat = 0.0;
  double lon = 0.0;
  return setup_link::parseLatLon(lat_str, lon_str, &lat, &lon) && save(lat, lon);
}

void clear() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.remove(kKeyLat);
  prefs.remove(kKeyLon);
  prefs.remove(kKeyChosen);
  prefs.end();
  s_lat = config::kDefaultRadarLat;
  s_lon = config::kDefaultRadarLon;
  s_saved = false;
}

}  // namespace services::location

#include "services/setup_session.h"

#include <WiFi.h>
#include <esp_system.h>
#include <setup_link/setup_link.h>

#include "config.h"
#include "services/radar_location.h"

namespace services::setup {

namespace {

char s_token[setup_link::kTokenLen + 1] = "";

bool saveLocation(double lat, double lon) { return location::save(lat, lon); }

}  // namespace

void init() { setup_link::makeToken(esp_random(), s_token); }

const char* token() { return s_token; }

bool url(char* out, size_t cap) {
  if (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    return false;
  }
  const String ip = WiFi.localIP().toString();
  return setup_link::buildSetupUrl(out, cap, config::kSetupPageUrl, ip.c_str(), s_token) != 0;
}

int handleLocation(const char* token, const char* lat, const char* lon,
                   const char** message) {
  switch (setup_link::handleLocationRequest(s_token, token, lat, lon, saveLocation)) {
    case setup_link::LocationResult::kSaved:
      *message = "Location saved. You can close this tab.";
      return 200;
    case setup_link::LocationResult::kBadToken:
      *message = "Wrong or missing setup code.";
      return 403;
    case setup_link::LocationResult::kBadCoordinates:
      *message = "Invalid latitude or longitude.";
      return 400;
    case setup_link::LocationResult::kSaveFailed:
      break;
  }
  *message = "Could not save the location.";
  return 500;
}

}  // namespace services::setup

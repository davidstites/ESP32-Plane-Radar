#include "setup_link/setup_link.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace setup_link {

namespace {

constexpr char kAlphabet[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
constexpr uint32_t kAlphabetLen = sizeof(kAlphabet) - 1;

bool parseCoord(const char* text, double* out) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  char* end = nullptr;
  const double v = strtod(text, &end);
  if (end == text || *end != '\0' || !std::isfinite(v)) {
    return false;
  }
  *out = v;
  return true;
}

}  // namespace

void makeToken(uint32_t random, char (&out)[kTokenLen + 1]) {
  for (size_t i = kTokenLen; i > 0; --i) {
    out[i - 1] = kAlphabet[random % kAlphabetLen];
    random /= kAlphabetLen;
  }
  out[kTokenLen] = '\0';
}

bool tokenMatches(const char* expected, const char* given) {
  if (expected == nullptr || given == nullptr) {
    return false;
  }
  const size_t len = strlen(expected);
  if (strlen(given) != len) {
    return false;
  }
  unsigned diff = 0;
  for (size_t i = 0; i < len; ++i) {
    diff |= static_cast<unsigned char>(expected[i] ^ given[i]);
  }
  return diff == 0;
}

size_t buildSetupUrl(char* out, size_t cap, const char* base, const char* ip,
                     const char* token) {
  if (out == nullptr || cap == 0) {
    return 0;
  }
  out[0] = '\0';
  if (base == nullptr || ip == nullptr || token == nullptr) {
    return 0;
  }
  const int n = snprintf(out, cap, "%s?device=%s&token=%s", base, ip, token);
  if (n < 0 || static_cast<size_t>(n) >= cap) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

bool parseLatLon(const char* lat_text, const char* lon_text, double* lat, double* lon) {
  double la = 0.0;
  double lo = 0.0;
  if (!parseCoord(lat_text, &la) || !parseCoord(lon_text, &lo)) {
    return false;
  }
  if (la < -90.0 || la > 90.0 || lo < -180.0 || lo > 180.0) {
    return false;
  }
  *lat = la;
  *lon = lo;
  return true;
}

LocationResult handleLocationRequest(const char* expected_token, const char* token,
                                     const char* lat_text, const char* lon_text,
                                     SaveFn save) {
  if (!tokenMatches(expected_token, token)) {
    return LocationResult::kBadToken;
  }
  double lat = 0.0;
  double lon = 0.0;
  if (!parseLatLon(lat_text, lon_text, &lat, &lon)) {
    return LocationResult::kBadCoordinates;
  }
  return save(lat, lon) ? LocationResult::kSaved : LocationResult::kSaveFailed;
}

}  // namespace setup_link

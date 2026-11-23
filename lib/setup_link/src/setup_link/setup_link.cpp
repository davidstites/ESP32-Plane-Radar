#include "setup_link/setup_link.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <qrcode.h>

namespace setup_link {

namespace {

constexpr char kAlphabet[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
constexpr uint32_t kAlphabetLen = sizeof(kAlphabet) - 1;

/** Byte-mode capacity at ECC low, versions 1..kQrMaxVersion. QRCode does not bounds-check. */
constexpr size_t kQrByteCapacity[kQrMaxVersion] = {17, 32, 53, 78, 106};

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
  if (len == 0 || strlen(given) != len) {
    return false;
  }
  unsigned diff = 0;
  for (size_t i = 0; i < len; ++i) {
    diff |= static_cast<unsigned char>(toupper(static_cast<unsigned char>(expected[i])) ^
                                       toupper(static_cast<unsigned char>(given[i])));
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

bool validLatLon(double lat, double lon) {
  return std::isfinite(lat) && std::isfinite(lon) && lat >= -90.0 && lat <= 90.0 &&
         lon >= -180.0 && lon <= 180.0;
}

bool parseLatLon(const char* lat_text, const char* lon_text, double* lat, double* lon) {
  double la = 0.0;
  double lo = 0.0;
  if (!parseCoord(lat_text, &la) || !parseCoord(lon_text, &lo)) {
    return false;
  }
  if (!validLatLon(la, lo)) {
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

bool QrCode::module(uint8_t x, uint8_t y) const {
  if (x >= size || y >= size) {
    return false;
  }
  const unsigned bit = static_cast<unsigned>(y) * size + x;
  return (data[bit / 8] >> (bit % 8)) & 1u;
}

bool encodeQr(const char* text, QrCode* out) {
  out->size = 0;
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  const size_t length = strlen(text);
  for (uint8_t version = 1; version <= kQrMaxVersion; ++version) {
    if (length > kQrByteCapacity[version - 1]) {
      continue;
    }
    uint8_t modules[(kQrMaxSize * kQrMaxSize + 7) / 8];
    ::QRCode qr;
    // Cannot fail here: the text fits this version and byte mode accepts any text.
    qrcode_initText(&qr, modules, version, ECC_LOW, text);
    memset(out->data, 0, sizeof(out->data));
    for (uint8_t y = 0; y < qr.size; ++y) {
      for (uint8_t x = 0; x < qr.size; ++x) {
        if (qrcode_getModule(&qr, x, y)) {
          const unsigned bit = static_cast<unsigned>(y) * qr.size + x;
          out->data[bit / 8] |= static_cast<uint8_t>(1u << (bit % 8));
        }
      }
    }
    out->size = qr.size;
    return true;
  }
  return false;
}

}  // namespace setup_link

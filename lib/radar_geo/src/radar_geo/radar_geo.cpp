#include "radar_geo/radar_geo.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace radar_geo {

const char* airportDisplayIdent(const char* icao) {
  const bool four_letters_with_k =
      icao[0] == 'K' && icao[1] != '\0' && icao[2] != '\0' && icao[3] != '\0' && icao[4] == '\0';
  return four_letters_with_k ? icao + 1 : icao;
}

bool parseRingRangeKm(const char* text, bool use_miles, float* out_km) {
  if (text == nullptr) {
    return false;
  }
  char* end = nullptr;
  const double value = std::strtod(text, &end);
  while (end != text && *end == ' ') {
    ++end;
  }
  if (end == text || *end != '\0' || !std::isfinite(value)) {
    return false;
  }
  const float unit_km = use_miles ? kKmPerMile : 1.0f;
  const float max_km = kMaxRingMiles * kKmPerMile;
  const float km = static_cast<float>(value) * unit_km;
  *out_km = std::min(std::max(km, unit_km), max_km);
  return true;
}

float nextRingKm(float current_km, const float* presets_km, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    if (presets_km[i] > current_km + 0.01f) {
      return presets_km[i];
    }
  }
  return presets_km[0];
}

bool sameView(const ViewKey& a, const ViewKey& b) {
  return a.center_lat == b.center_lat && a.center_lon == b.center_lon &&
         a.ring3_km == b.ring3_km;
}

OffsetKm offsetKm(double center_lat, double center_lon, float lat, float lon) {
  // Longitude degrees shrink toward the poles; scale by cos(latitude) so
  // east-west distance isn't overstated away from the equator.
  const float center_lat_rad = static_cast<float>(center_lat) * kDegToRad;
  OffsetKm o;
  o.dx = static_cast<float>(lon - center_lon) * kKmPerDeg * std::cos(center_lat_rad);
  o.dy = static_cast<float>(lat - center_lat) * kKmPerDeg;
  o.dist = std::sqrt(o.dx * o.dx + o.dy * o.dy);
  return o;
}

LatLon deadReckon(float lat, float lon, float track_deg, float gs_knots, uint32_t elapsed_ms) {
  constexpr float kKmPerNm = 1.852f;
  constexpr float kMsPerHour = 3600000.0f;
  const float dist_km = gs_knots * kKmPerNm * (static_cast<float>(elapsed_ms) / kMsPerHour);
  const float rad = track_deg * kDegToRad;
  return {lat + dist_km * std::cos(rad) / kKmPerDeg,
          lon + dist_km * std::sin(rad) / (kKmPerDeg * std::cos(lat * kDegToRad))};
}

ScreenPoint toScreen(const OffsetKm& offset, float outer_km, int outer_radius_px, int center_x,
                     int center_y) {
  const float px_per_km = static_cast<float>(outer_radius_px) / outer_km;
  return {center_x + static_cast<int>(std::lround(offset.dx * px_per_km)),
          center_y - static_cast<int>(std::lround(offset.dy * px_per_km))};
}

int distSq(int x, int y, int center_x, int center_y) {
  const int dx = x - center_x;
  const int dy = y - center_y;
  return dx * dx + dy * dy;
}

ScreenPoint clipToDisc(ScreenPoint from, ScreenPoint to, int center_x, int center_y,
                       int radius) {
  const int radius_sq = radius * radius;
  if (distSq(to.x, to.y, center_x, center_y) <= radius_sq) {
    return to;
  }

  const int dx = to.x - from.x;
  const int dy = to.y - from.y;
  for (float t = 1.0f; t > 0.0f; t -= 0.05f) {
    const ScreenPoint p = {from.x + static_cast<int>(std::lround(dx * t)),
                           from.y + static_cast<int>(std::lround(dy * t))};
    if (distSq(p.x, p.y, center_x, center_y) <= radius_sq) {
      return p;
    }
  }
  return from;
}

bool rectsOverlap(const Rect& a, const Rect& b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

int firstClearCandidate(const Rect* candidates, size_t candidate_count, const Rect* placed,
                        size_t placed_count) {
  for (size_t c = 0; c < candidate_count; ++c) {
    bool clear = true;
    for (size_t p = 0; p < placed_count && clear; ++p) {
      clear = !rectsOverlap(candidates[c], placed[p]);
    }
    if (clear) {
      return static_cast<int>(c);
    }
  }
  return -1;
}

}  // namespace radar_geo

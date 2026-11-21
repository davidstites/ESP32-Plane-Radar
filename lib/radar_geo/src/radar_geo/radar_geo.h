#pragma once

#include <cstdint>

namespace radar_geo {

/** Flat-earth approximation: 1° of latitude ≈ 111 km. */
constexpr float kKmPerDeg = 111.0f;
constexpr float kDegToRad = 0.01745329252f;

/** Offset from the radar center in km: x east, y north, and straight-line distance. */
struct OffsetKm {
  float dx;
  float dy;
  float dist;
};

struct LatLon {
  float lat;
  float lon;
};

struct ScreenPoint {
  int x;
  int y;
};

/** Offset of (lat, lon) from (center_lat, center_lon); east-west scaled by cos(center_lat). */
OffsetKm offsetKm(double center_lat, double center_lon, float lat, float lon);

/**
 * Position after flying gs_knots along track_deg (0 = N, 90 = E) for elapsed_ms, on the same
 * flat projection as offsetKm(): east-west degrees are widened by 1/cos(lat) so the drawn
 * distance matches the distance flown.
 */
LatLon deadReckon(float lat, float lon, float track_deg, float gs_knots, uint32_t elapsed_ms);

/** Screen pixel for an offset: outer_km lands on outer_radius_px from center, north up. */
ScreenPoint toScreen(const OffsetKm& offset, float outer_km, int outer_radius_px, int center_x,
                     int center_y);

/** Squared pixel distance of (x, y) from (center_x, center_y). */
int distSq(int x, int y, int center_x, int center_y);

/**
 * to, unless it lies outside the disc: then the point 95%, 90%, ... of the way from
 * from to to that is inside, or from if no step is.
 */
ScreenPoint clipToDisc(ScreenPoint from, ScreenPoint to, int center_x, int center_y,
                       int radius);

}  // namespace radar_geo

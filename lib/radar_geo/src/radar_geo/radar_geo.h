#pragma once

#include <cstddef>
#include <cstdint>

namespace radar_geo {

/** Flat-earth approximation: 1° of latitude ≈ 111 km. */
constexpr float kKmPerDeg = 111.0f;
constexpr float kDegToRad = 0.01745329252f;
constexpr float kKmPerMile = 1.609344f;
/** Largest radar range (the scale shown on the ring) a user can set. */
constexpr float kMaxRingMiles = 25.0f;

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

/**
 * ICAO code as shown on screen: a leading K (contiguous US) is dropped from 4-letter codes,
 * so KSJC reads SJC and KE16 reads E16. Other codes are returned unchanged. Points into icao.
 */
const char* airportDisplayIdent(const char* icao);

/**
 * Parse a range typed in the portal, in miles or km, into km. Clamped to between one unit
 * (1 mi or 1 km) and kMaxRingMiles. Returns false, leaving *out_km alone, if text is not a number.
 */
bool parseRingRangeKm(const char* text, bool use_miles, float* out_km);

/** The first preset larger than current_km, or the first preset when none is larger. */
float nextRingKm(float current_km, const float* presets_km, size_t count);

/** What the projection depends on: a layer drawn from it stays valid while the key is equal. */
struct ViewKey {
  double center_lat;
  double center_lon;
  float ring3_km;
};

bool sameView(const ViewKey& a, const ViewKey& b);

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

/** Axis-aligned pixel rectangle: x, y is the top-left corner. */
struct Rect {
  int x;
  int y;
  int w;
  int h;
};

/** True when the rectangles share area; rectangles that only touch along an edge do not overlap. */
bool rectsOverlap(const Rect& a, const Rect& b);

/**
 * True when the segment (x0, y0)-(x1, y1) touches the rectangle, edges included. Callers
 * widen the rectangle by half the line width to test a thick line.
 */
bool segmentIntersectsRect(int x0, int y0, int x1, int y1, const Rect& rect);

/**
 * Index of the first candidate that overlaps none of the placed rectangles, or -1 if
 * every candidate collides. Candidates are tried in order, so put the preferred one first.
 */
int firstClearCandidate(const Rect* candidates, size_t candidate_count, const Rect* placed,
                        size_t placed_count);

}  // namespace radar_geo

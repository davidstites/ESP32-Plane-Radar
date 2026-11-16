#include "ui/radar_projection.h"

#include <radar_geo/radar_geo.h>

#include "services/radar_location.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"

namespace ui::radar {

namespace {

radar_geo::OffsetKm offsetFromCenter(float lat, float lon) {
  return radar_geo::offsetKm(services::location::lat(), services::location::lon(), lat, lon);
}

}  // namespace

void offsetKmFromCenter(float lat, float lon, float* dx_km, float* dy_km, float* dist_km) {
  const radar_geo::OffsetKm o = offsetFromCenter(lat, lon);
  *dx_km = o.dx;
  *dy_km = o.dy;
  *dist_km = o.dist;
}

void latLonToScreen(float lat, float lon, int* out_x, int* out_y) {
  const radar_geo::ScreenPoint p =
      radar_geo::toScreen(offsetFromCenter(lat, lon), rangeCurrent().outer_km,
                          kGridOuterRadius, kCenterX, kCenterY);
  *out_x = p.x;
  *out_y = p.y;
}

int distSqFromCenter(int x, int y) { return radar_geo::distSq(x, y, kCenterX, kCenterY); }

void clipPointToOuterRing(int x0, int y0, int* x1, int* y1) {
  const radar_geo::ScreenPoint p =
      radar_geo::clipToDisc({x0, y0}, {*x1, *y1}, kCenterX, kCenterY, kGridOuterRadius);
  *x1 = p.x;
  *y1 = p.y;
}

}  // namespace ui::radar

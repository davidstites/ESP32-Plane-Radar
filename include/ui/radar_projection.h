#pragma once

namespace ui::radar {

/** km offset of (lat, lon) from the radar center (x east, y north) and its distance. */
void offsetKmFromCenter(float lat, float lon, float* dx_km, float* dy_km, float* dist_km);

/** Screen pixel for (lat, lon) at the current range preset; north = screen up. */
void latLonToScreen(float lat, float lon, int* out_x, int* out_y);

/** Squared pixel distance of (x, y) from the screen center. */
int distSqFromCenter(int x, int y);

/** Pull (x1, y1) back toward (x0, y0) until it is inside the outer grid ring. */
void clipPointToOuterRing(int x0, int y0, int* x1, int* y1);

}  // namespace ui::radar

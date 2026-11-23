#pragma once

#include <nexrad_decode/precip_grid.h>
#include <radar_geo/radar_geo.h>

namespace services::precip {

/** Create the internal lock. Call once before any fetch or draw. */
void init();

/**
 * Fetch the NEXRAD base-reflectivity image for the view (center and range) and store it
 * as the precipitation grid. Plain HTTP, opening and closing its own connection. Returns
 * false on any failure, leaving the previous grid in place.
 */
bool fetch(const radar_geo::ViewKey& view, float outer_km);

/**
 * Hold the grid steady while drawing it. lock() returns the grid and the view it was fetched
 * for, or nullptr if there is none yet; call unlock() when done with it (also after nullptr).
 */
const nexrad::Grid* lock(radar_geo::ViewKey* view);
void unlock();

}  // namespace services::precip

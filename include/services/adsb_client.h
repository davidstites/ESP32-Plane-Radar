#pragma once

#include <cstddef>

#include "adsb_parse/aircraft_parser.h"

namespace services::adsb {

using Aircraft = adsb_parse::Aircraft;
using adsb_parse::kMaxAircraft;

/**
 * Close the kept-alive TLS session, freeing its heap for another connection. Call only from
 * the task that runs fetchUpdate(); the next fetchUpdate() reconnects.
 */
void closeConnection();

/** Create the internal lock. Call once before any fetch/snapshot. */
void init();

/**
 * Copy the current aircraft into out (up to max_out) and report the fetch time,
 * under the lock: safe from a different thread than the one running fetchUpdate().
 * Returns the number copied.
 */
size_t snapshotAircraft(Aircraft* out, size_t max_out, unsigned long* out_last_update_ms);

/**
 * Fetch aircraft within fetch_radius_km of center_lat/lon from adsb.fi and publish
 * them for snapshotAircraft(). Stamps the publish time (millis()) as the
 * dead-reckoning base; the stored fixes are as of that time.
 */
bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km);

}  // namespace services::adsb

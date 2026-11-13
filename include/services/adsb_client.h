#pragma once

#include <cstddef>

#include "adsb_parse/aircraft_parser.h"

namespace services::adsb {

using Aircraft = adsb_parse::Aircraft;
using adsb_parse::kMaxAircraft;

size_t aircraftCount();
const Aircraft* aircraftList();

/** Hook invoked during long HTTP I/O (e.g. wifiLoop). Optional. */
using PollFn = void (*)();
void setPollFn(PollFn fn);

/** Fetch aircraft within fetch_radius_km of center_lat/lon from adsb.fi. */
bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km);

}  // namespace services::adsb

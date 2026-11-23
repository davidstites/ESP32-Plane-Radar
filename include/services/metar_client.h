#pragma once

#include <metar_parse/metar_parser.h>

#include <cstddef>

namespace services::metar {

/** Create the internal lock. Call once before any fetch/category lookup. */
void init();

/**
 * Fetch the latest flight category for each airport (ICAO ids) from aviationweather.gov
 * and publish them for category(). Opens and closes its own TLS session. Returns false on
 * any failure, leaving the previously published categories in place.
 */
bool fetch(const char* const* icaos, size_t count);

/** Latest category for an ICAO id; kUnknown when there is no recent report for it. */
metar_parse::Category category(const char* icao);

}  // namespace services::metar

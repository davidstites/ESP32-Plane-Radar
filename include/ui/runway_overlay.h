#pragma once

#include <LovyanGFX.hpp>

namespace ui::runway {

void drawLargeAirportRunways(lgfx::LGFXBase& gfx);

/** True once the airports in view are known (the first frame with runways shown has drawn). */
bool labelsReady();

/**
 * Copy the ICAO ids of the airports currently labeled into out (up to max); returns the
 * count. Safe to call from another task than the one that draws.
 */
size_t snapshotLabelIcaos(const char** out, size_t max);

}  // namespace ui::runway

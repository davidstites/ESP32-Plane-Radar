#include "ui/precip_overlay.h"

#include <radar_geo/radar_geo.h>

#include "hardware/panel_color.h"
#include "services/precip_client.h"
#include "services/radar_location.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"

namespace ui::precip {

namespace {

constexpr int kCellPx = radar::kSize / nexrad::kGridDim;

uint16_t levelColor(nexrad::Level level) {
  switch (level) {
    case nexrad::Level::kLight:
      return panelColor565(radar::kPrecipLightR, radar::kPrecipLightG, radar::kPrecipLightB);
    case nexrad::Level::kModerate:
      return panelColor565(radar::kPrecipModerateR, radar::kPrecipModerateG,
                           radar::kPrecipModerateB);
    case nexrad::Level::kHeavy:
      return panelColor565(radar::kPrecipHeavyR, radar::kPrecipHeavyG, radar::kPrecipHeavyB);
    case nexrad::Level::kNone:
      break;
  }
  return 0;
}

void drawGrid(lgfx::LGFXBase& gfx, const nexrad::Grid& grid) {
  const uint16_t colors[] = {0, levelColor(nexrad::Level::kLight),
                             levelColor(nexrad::Level::kModerate),
                             levelColor(nexrad::Level::kHeavy)};
  for (int y = 0; y < nexrad::kGridDim; ++y) {
    int x = 0;
    while (x < nexrad::kGridDim) {
      const nexrad::Level level = grid.get(x, y);
      if (level == nexrad::Level::kNone) {
        ++x;
        continue;
      }
      // One fill per run of equal cells in the row.
      int run = 1;
      while (x + run < nexrad::kGridDim && grid.get(x + run, y) == level) {
        ++run;
      }
      gfx.fillRect(x * kCellPx, y * kCellPx, run * kCellPx, kCellPx,
                   colors[static_cast<int>(level)]);
      x += run;
    }
  }
}

}  // namespace

bool draw(lgfx::LGFXBase& gfx) {
  if (!radar::showPrecip()) {
    return false;
  }
  radar_geo::ViewKey fetched_for = {};
  const nexrad::Grid* grid = services::precip::lock(&fetched_for);
  // A grid fetched for another center or range would be misplaced: show nothing until the
  // refetch for the new view arrives.
  const radar_geo::ViewKey now = {services::location::lat(), services::location::lon(),
                                  radar::rangeCurrent().ring3_km};
  const bool matches = grid != nullptr && radar_geo::sameView(fetched_for, now);
  if (matches) {
    drawGrid(gfx, *grid);
  }
  services::precip::unlock();
  return matches;
}

}  // namespace ui::precip

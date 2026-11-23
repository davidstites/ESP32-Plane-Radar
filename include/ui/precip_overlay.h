#pragma once

#include <LovyanGFX.hpp>

namespace ui::precip {

/**
 * Draw the faint precipitation layer behind the grid, if it is switched on and the radar image
 * on hand was fetched for the view being shown. Returns whether it drew anything, so the grid
 * can thin its rings to match.
 */
bool draw(lgfx::LGFXBase& gfx);

}  // namespace ui::precip

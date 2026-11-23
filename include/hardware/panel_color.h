#pragma once

#include <LovyanGFX.hpp>

#include <cstdint>

#include "config.h"

/**
 * RGB565 for the color you want to SEE on screen. The GC9A01 panel is wired BGR, so red and
 * blue are swapped (as initPalette() does for the aircraft) when kDisplayRgbOrder is set.
 */
inline uint16_t panelColor565(uint8_t r, uint8_t g, uint8_t b) {
  return config::kDisplayRgbOrder ? lgfx::color565(b, g, r) : lgfx::color565(r, g, b);
}

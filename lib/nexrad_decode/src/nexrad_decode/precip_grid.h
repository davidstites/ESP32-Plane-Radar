#pragma once

#include <cstddef>
#include <cstdint>

namespace nexrad {

/** Cells per side: the radar image is fetched at this size and drawn at 2 screen px per cell. */
constexpr int kGridDim = 120;

enum class Level : uint8_t { kNone = 0, kLight, kModerate, kHeavy };

/**
 * Precipitation level of one radar pixel from its palette color: transparent is none; red and
 * magenta are heavy; yellow and orange are moderate; green, cyan and blue are light.
 */
Level classifyColor(uint8_t alpha, uint8_t r, uint8_t g, uint8_t b);

/** kGridDim x kGridDim precipitation levels, two bits per cell. */
class Grid {
 public:
  void clear();
  /** kNone outside the grid. */
  Level get(int x, int y) const;
  /** Ignored outside the grid. */
  void set(int x, int y, Level level);
  /** Blank every cell whose center is farther than radius_cells from the grid's center. */
  void maskOutsideDisc(int radius_cells);
  size_t count(Level level) const;

 private:
  static constexpr size_t kBytes = static_cast<size_t>(kGridDim) * kGridDim / 4;
  uint8_t cells_[kBytes] = {};
};

}  // namespace nexrad

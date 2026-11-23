#include "nexrad_decode/precip_grid.h"

namespace nexrad {

namespace {

constexpr uint8_t kMinOpaqueAlpha = 16;
constexpr int kStrongRed = 150;
constexpr int kLowGreenForHeavy = 120;

bool inside(int x, int y) { return x >= 0 && x < kGridDim && y >= 0 && y < kGridDim; }

}  // namespace

Level classifyColor(uint8_t alpha, uint8_t r, uint8_t g, uint8_t /*b*/) {
  if (alpha < kMinOpaqueAlpha) {
    return Level::kNone;
  }
  if (r > kStrongRed) {
    return g < kLowGreenForHeavy ? Level::kHeavy : Level::kModerate;
  }
  return Level::kLight;
}

void Grid::clear() {
  for (uint8_t& byte : cells_) {
    byte = 0;
  }
}

Level Grid::get(int x, int y) const {
  if (!inside(x, y)) {
    return Level::kNone;
  }
  const size_t i = static_cast<size_t>(y) * kGridDim + static_cast<size_t>(x);
  return static_cast<Level>((cells_[i >> 2] >> ((i & 3) * 2)) & 3);
}

void Grid::set(int x, int y, Level level) {
  if (!inside(x, y)) {
    return;
  }
  const size_t i = static_cast<size_t>(y) * kGridDim + static_cast<size_t>(x);
  const unsigned shift = (i & 3) * 2;
  cells_[i >> 2] = static_cast<uint8_t>((cells_[i >> 2] & ~(3u << shift)) |
                                        (static_cast<unsigned>(level) << shift));
}

void Grid::maskOutsideDisc(int radius_cells) {
  const int center = kGridDim / 2;
  const int radius_sq = radius_cells * radius_cells;
  for (int y = 0; y < kGridDim; ++y) {
    for (int x = 0; x < kGridDim; ++x) {
      const int dx = x - center;
      const int dy = y - center;
      if (dx * dx + dy * dy > radius_sq) {
        set(x, y, Level::kNone);
      }
    }
  }
}

size_t Grid::count(Level level) const {
  size_t n = 0;
  for (int y = 0; y < kGridDim; ++y) {
    for (int x = 0; x < kGridDim; ++x) {
      n += get(x, y) == level ? 1 : 0;
    }
  }
  return n;
}

}  // namespace nexrad

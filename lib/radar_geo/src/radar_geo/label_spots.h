#pragma once

#include <cstddef>
#include <cstdint>

#include "radar_geo/radar_geo.h"

namespace radar_geo {

/**
 * Index of the spot to put a label in, or -1 if none is free. A spot is free when it overlaps
 * no `avoid` rectangle (other labels) and, preferably, no `prefer_clear` one (other aircraft
 * symbols). Spots clear of both are taken first; failing that, spots clear of `avoid` alone.
 * Within a tier `preferred` (if a valid index) is tried first, so a label stays where it was
 * for as long as that spot stays free, then the spots in order.
 */
int pickClearSpot(const Rect* spots, size_t spot_count, int preferred, const Rect* avoid,
                  size_t avoid_count, const Rect* prefer_clear, size_t prefer_clear_count);

/** Where an aircraft tag block can go around its symbol, in the order they are tried. */
constexpr size_t kTagSpotCount = 8;

struct TagSpot {
  Rect rect;
  /** Text hugs the rectangle's right edge (tag left of the symbol) rather than its left. */
  bool right_aligned;
};

/**
 * The tag block (block_w x block_h) around a symbol at (x, y) whose half-size is symbol_half,
 * kept gap px clear of it and inside the square screen. Order: the side facing the center
 * (right when prefer_right), the other side, those two shifted up then down by half a block,
 * then centered above and below.
 */
void tagSpots(int x, int y, int block_w, int block_h, int symbol_half, int gap, int screen_size,
              bool prefer_right, TagSpot* out);

/** Stable 32-bit hash of text (FNV-1a); 0 is never returned, so 0 can mean "no key". */
uint32_t hashLabelKey(const char* text);

/**
 * Remembers, per aircraft, which spot its tag last used, so tags do not hop between spots from
 * one frame to the next. Entries untouched for kExpireFrames are forgotten.
 */
class SpotMemory {
 public:
  static constexpr size_t kCapacity = 64;
  static constexpr uint32_t kExpireFrames = 40;

  /** The spot last stored for key, or -1 if none or expired. A zero key is never stored. */
  int get(uint32_t key, uint32_t frame) const;
  void set(uint32_t key, int spot, uint32_t frame);

 private:
  struct Entry {
    uint32_t key = 0;
    uint32_t frame = 0;
    int spot = -1;
  };
  bool live(const Entry& e, uint32_t frame) const;

  Entry entries_[kCapacity];
};

}  // namespace radar_geo

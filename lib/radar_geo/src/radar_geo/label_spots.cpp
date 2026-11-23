#include "radar_geo/label_spots.h"

#include <algorithm>
#include <iterator>

namespace radar_geo {

namespace {

bool overlapsAny(const Rect& r, const Rect* others, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    if (rectsOverlap(r, others[i])) {
      return true;
    }
  }
  return false;
}

}  // namespace

int pickClearSpot(const Rect* spots, size_t spot_count, int preferred, const Rect* avoid,
                  size_t avoid_count, const Rect* prefer_clear, size_t prefer_clear_count) {
  const bool has_preferred = preferred >= 0 && static_cast<size_t>(preferred) < spot_count;
  for (int tier = 0; tier < 2; ++tier) {
    const bool strict = tier == 0;
    for (size_t n = 0; n < spot_count + (has_preferred ? 1 : 0); ++n) {
      // Visit the preferred spot first, then every spot in order (preferred again is harmless).
      const size_t i = (has_preferred && n == 0) ? static_cast<size_t>(preferred)
                                                 : n - (has_preferred ? 1 : 0);
      if (overlapsAny(spots[i], avoid, avoid_count)) {
        continue;
      }
      if (strict && overlapsAny(spots[i], prefer_clear, prefer_clear_count)) {
        continue;
      }
      return static_cast<int>(i);
    }
  }
  return -1;
}

void tagSpots(int x, int y, int block_w, int block_h, int symbol_half, int gap, int screen_size,
              bool prefer_right, TagSpot* out) {
  const int right_x = x + symbol_half + gap;
  const int left_x = x - symbol_half - gap - block_w;
  const int side_x[2] = {prefer_right ? right_x : left_x, prefer_right ? left_x : right_x};
  const bool side_right_aligned[2] = {!prefer_right, prefer_right};
  const int centered_y = y - block_h / 2;
  const int shift = block_h / 2;

  const int xs[kTagSpotCount] = {side_x[0], side_x[1], side_x[0], side_x[0],
                                 side_x[1], side_x[1], x - block_w / 2, x - block_w / 2};
  const int ys[kTagSpotCount] = {centered_y,         centered_y,         centered_y - shift,
                                 centered_y + shift, centered_y - shift, centered_y + shift,
                                 y - symbol_half - gap - block_h, y + symbol_half + gap};
  const bool right_aligned[kTagSpotCount] = {side_right_aligned[0], side_right_aligned[1],
                                             side_right_aligned[0], side_right_aligned[0],
                                             side_right_aligned[1], side_right_aligned[1],
                                             false,                 false};
  for (size_t i = 0; i < kTagSpotCount; ++i) {
    const int max_x = std::max(1, screen_size - block_w - 1);
    const int max_y = std::max(1, screen_size - block_h - 1);
    out[i].rect = {std::min(std::max(xs[i], 1), max_x), std::min(std::max(ys[i], 1), max_y),
                   block_w, block_h};
    out[i].right_aligned = right_aligned[i];
  }
}

uint32_t hashLabelKey(const char* text) {
  uint32_t hash = 2166136261u;
  for (const char* p = text; *p != '\0'; ++p) {
    hash = (hash ^ static_cast<uint8_t>(*p)) * 16777619u;
  }
  return hash == 0 ? 1u : hash;
}

bool SpotMemory::live(const Entry& e, uint32_t frame) const {
  return e.key != 0 && frame - e.frame < kExpireFrames;
}

int SpotMemory::get(uint32_t key, uint32_t frame) const {
  if (key == 0) {
    return -1;
  }
  for (const Entry& e : entries_) {
    if (e.key == key && live(e, frame)) {
      return e.spot;
    }
  }
  return -1;
}

void SpotMemory::set(uint32_t key, int spot, uint32_t frame) {
  if (key == 0) {
    return;
  }
  Entry* target = nullptr;
  for (Entry& e : entries_) {
    if (e.key == key) {
      target = &e;  // refresh this aircraft's entry in place
      break;
    }
  }
  if (target == nullptr) {
    for (Entry& e : entries_) {
      if (!live(e, frame)) {
        target = &e;  // a free or expired slot
        break;
      }
    }
  }
  if (target == nullptr) {
    target = &*std::min_element(std::begin(entries_), std::end(entries_),
                                [frame](const Entry& a, const Entry& b) {
                                  return frame - a.frame > frame - b.frame;  // the oldest
                                });
  }
  target->key = key;
  target->frame = frame;
  target->spot = spot;
}

}  // namespace radar_geo

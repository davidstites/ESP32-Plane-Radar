#include "button_logic/button_tracker.h"

namespace button_logic {

void ButtonTracker::onEdge(bool down, uint32_t now_ms) {
  if (down) {
    is_down_ = true;
    down_ms_ = now_ms;
  } else if (is_down_) {
    const uint32_t held = now_ms - down_ms_;
    if (held >= tap_min_ms_ && held < hold_ms_) {
      tap_pending_ = true;
    }
    is_down_ = false;
  }
}

bool ButtonTracker::pollHeld(bool down, uint32_t now_ms) {
  if (!down) {
    is_down_ = false;
    long_press_handled_ = false;
    return false;
  }
  if (!is_down_) {
    is_down_ = true;
    down_ms_ = now_ms;
  }
  if (!long_press_handled_ && now_ms - down_ms_ >= hold_ms_) {
    long_press_handled_ = true;
    return true;
  }
  return false;
}

bool ButtonTracker::consumeTap() {
  const bool tap = tap_pending_;
  tap_pending_ = false;
  return tap;
}

}  // namespace button_logic

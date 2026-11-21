#pragma once

#include <cstdint>

namespace button_logic {

/**
 * Tap / long-press state machine for one button. No hardware access and no
 * locking: the caller supplies the pin level and a millisecond clock, and
 * serialises calls (the firmware wraps them in a critical section).
 */
class ButtonTracker {
 public:
  ButtonTracker(uint32_t tap_min_ms, uint32_t hold_ms)
      : tap_min_ms_(tap_min_ms), hold_ms_(hold_ms) {}

  /** Pin level changed (interrupt path). A release inside [tap_min, hold) latches a tap. */
  void onEdge(bool down, uint32_t now_ms);

  /**
   * Level sampled from the main loop. Catches a press whose edge was missed and
   * returns true once per hold, when it reaches hold_ms.
   */
  bool pollHeld(bool down, uint32_t now_ms);

  /** Returns and clears the latched tap. */
  bool consumeTap();

 private:
  uint32_t tap_min_ms_;
  uint32_t hold_ms_;
  bool is_down_ = false;
  uint32_t down_ms_ = 0;
  bool tap_pending_ = false;
  bool long_press_handled_ = false;
};

}  // namespace button_logic

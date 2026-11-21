#include "hardware/boot_button.h"

#include <Arduino.h>

#include "button_logic/button_tracker.h"
#include "config.h"

namespace {

portMUX_TYPE s_boot_mux = portMUX_INITIALIZER_UNLOCKED;
// Shared between the ISR and the main loop; every access holds s_boot_mux.
button_logic::ButtonTracker s_tracker(config::kBootTapMinMs,
                                      config::kBootResetHoldMs);
bool s_boot_interrupt_attached = false;
BootLongPressFn s_long_press_fn = nullptr;

void IRAM_ATTR onBootButtonIsr() {
  const bool down = digitalRead(config::kBootPin) == LOW;
  const uint32_t now = millis();
  portENTER_CRITICAL_ISR(&s_boot_mux);
  s_tracker.onEdge(down, now);
  portEXIT_CRITICAL_ISR(&s_boot_mux);
}

}  // namespace

void bootButtonInit() {
  pinMode(config::kBootPin, INPUT_PULLUP);
  if (s_boot_interrupt_attached) {
    return;
  }
  attachInterrupt(digitalPinToInterrupt(static_cast<uint8_t>(config::kBootPin)),
                  onBootButtonIsr, CHANGE);
  s_boot_interrupt_attached = true;
}

void bootButtonSetLongPressHandler(BootLongPressFn fn) { s_long_press_fn = fn; }

bool bootButtonConsumeTap() {
  portENTER_CRITICAL(&s_boot_mux);
  const bool tap = s_tracker.consumeTap();
  portEXIT_CRITICAL(&s_boot_mux);
  return tap;
}

void bootButtonPollLongPress() {
  const bool down = digitalRead(config::kBootPin) == LOW;
  portENTER_CRITICAL(&s_boot_mux);
  const bool long_press = s_tracker.pollHeld(down, millis());
  portEXIT_CRITICAL(&s_boot_mux);
  if (long_press && s_long_press_fn != nullptr) {
    s_long_press_fn();
  }
}

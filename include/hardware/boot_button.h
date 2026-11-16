#pragma once

/** Runs when BOOT has been held for config::kBootResetHoldMs. */
using BootLongPressFn = void (*)();

/** GPIO + interrupt setup; call once early in setup() (later calls re-apply the pull-up). */
void bootButtonInit();
/** Set the long-hold action; register it before anything calls bootButtonPollLongPress(). */
void bootButtonSetLongPressHandler(BootLongPressFn fn);
/** Latched short tap (survives blocking HTTP/display work). */
bool bootButtonConsumeTap();
/** Call each loop iteration and in blocking waits; runs the long-press handler once per hold. */
void bootButtonPollLongPress();

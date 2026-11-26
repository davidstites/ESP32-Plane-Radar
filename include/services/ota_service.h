#pragma once

#include <cstddef>
#include <cstdint>

#include "ota_core/updater.h"

namespace services::ota {

/**
 * Build the updater from this board's identity and reconcile the last update attempt with
 * what is running now (roll back, blacklist, or confirm pending). Call right after the
 * Serial banner in setup().
 */
void init();

/**
 * Call right after init(), before the display and Wi-Fi code. Watches BOOT for the first
 * kOtaRecoveryWindowMs after power-up; if it is held for kOtaRecoveryHoldMs the running build
 * is blacklisted and the board restarts into the previous firmware. Does not return in that
 * case; otherwise it returns once the window has passed and the button is up.
 */
void recoveryWindow();

/** True while checkNow() runs: the render loop skips redraws so they cannot take heap mid-handshake. */
bool checkRunning();

/** Run one update check. The caller must have closed other TLS sessions first. */
ota_core::CheckResult checkNow();

/**
 * Call from loop(). Arms the loop watchdog once while a new build is unconfirmed, and confirms
 * it after kOtaConfirmAfterMs of running (floor raised, marked valid, watchdog off). The
 * network is never consulted: only a crash or hang before then rolls back and blacklists.
 */
void confirmTick();

/**
 * Feeds the loop watchdog only while confirmTick() has it armed. Long waits outside loop()
 * (Wi-Fi connect) call this; feeding an unarmed watchdog logs "Failed to feed WDT! Error: 261".
 */
void feedLoopWdtIfArmed();

/** Pending build in NVS (0 when none), the blacklist, and the running slot's label. */
uint32_t pendingBuild();
size_t blacklistedBuilds(uint32_t out[8]);
const char* runningSlotLabel();

}  // namespace services::ota

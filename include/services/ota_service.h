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

/** Run one update check. The caller must have closed other TLS sessions first. */
ota_core::CheckResult checkNow();

/**
 * Call from loop(). Arms the loop watchdog once while a new build is unconfirmed, and confirms
 * it after kOtaConfirmAfterMs of running (floor raised, marked valid, watchdog off). The
 * network is never consulted: only a crash or hang before then rolls back and blacklists.
 */
void confirmTick();

/** Pending build in NVS (0 when none), the blacklist, and the running slot's label. */
uint32_t pendingBuild();
size_t blacklistedBuilds(uint32_t out[8]);
const char* runningSlotLabel();

}  // namespace services::ota

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

/** True while the running build is new and not yet confirmed. */
bool pendingVerify();

/**
 * Health probe for an unconfirmed build, run on the network task where checkNow() would run: one
 * GET of the manifest from the OTA host. On success the build is confirmed (floor raised, marked
 * valid, loop watchdog off) and true is returned. Only a crash or hang blacklists a build;
 * failing to confirm never does.
 */
bool probeNow();

/**
 * Call from loop(). Arms the loop watchdog once while unconfirmed, and if kOtaConfirmDeadlineMs
 * pass after boot without a confirmation, restarts so the bootloader rolls back (recorded as
 * "unconfirmed", never blacklisted).
 */
void confirmTick();

/** Pending build in NVS (0 when none), the blacklist, and the running slot's label. */
uint32_t pendingBuild();
size_t blacklistedBuilds(uint32_t out[8]);
const char* runningSlotLabel();

}  // namespace services::ota

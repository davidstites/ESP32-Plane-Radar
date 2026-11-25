#pragma once

#include <cstddef>
#include <cstdint>

#include "ota_core/hal.h"
#include "ota_core/state.h"
#include "ota_core/types.h"

namespace ota_core {

struct Config {
  const char* basePath;  // same-host directory holding manifest.json and the images
  uint8_t deviceId[kDeviceIdBytes];
  uint32_t runningBuild;
  KeyList keys;
  Ed25519VerifyFn verify;
};

struct BootInfo {
  bool pendingVerify;  // the running image is new and not yet confirmed
  bool rolledBack;     // an update failed and the bootloader reverted it
  uint32_t rolledBackBuild;
};

class Updater {
 public:
  // `http` is unused until the check path lands, but the constructor is final.
  Updater(const Config& cfg, IOtaStore& store, IOtaSlots& slots, IOtaHttp& /*http*/)
      : cfg_(cfg), state_(store), slots_(slots) {}

  /**
   * Reconcile the stored pending build with what is actually running. A pending build that
   * is not the running one either failed and was rolled back (blacklist it) or is stale from
   * a USB flash (just forget it).
   */
  BootInfo onBoot();

  /**
   * Call once the running image has proven itself. Marks it valid with the bootloader, then
   * raises the anti-rollback floor and clears the pending build. This is the only place the
   * floor rises. Returns true without writing anything when the image is already valid.
   */
  bool confirmBoot();

 private:
  Config cfg_;
  OtaState state_;
  IOtaSlots& slots_;
};

}  // namespace ota_core

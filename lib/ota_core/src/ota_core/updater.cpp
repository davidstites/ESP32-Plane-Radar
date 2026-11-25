#include "ota_core/updater.h"

namespace ota_core {

BootInfo Updater::onBoot() {
  BootInfo info{};
  info.pendingVerify = slots_.runningState() == SlotState::kPendingVerify;

  const uint32_t pending = state_.pending();
  if (pending != 0 && pending != cfg_.runningBuild) {
    if (slots_.hasInvalidSlot() && state_.blacklist(pending)) {
      info.rolledBack = true;
      info.rolledBackBuild = pending;
    }
    state_.clearPending();
  }
  return info;
}

bool Updater::confirmBoot() {
  if (slots_.runningState() != SlotState::kPendingVerify) return true;
  if (!state_.ok() || !slots_.markRunningValid()) return false;
  const bool raised = state_.raiseFloor(cfg_.runningBuild);
  const bool cleared = state_.clearPending();
  return raised && cleared;
}

}  // namespace ota_core

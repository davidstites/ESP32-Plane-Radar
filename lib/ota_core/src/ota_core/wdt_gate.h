#pragma once

#include <atomic>

namespace ota_core {

// Tracks whether the loop watchdog is armed. Feeding it while unarmed (before confirmTick()
// enables it, or after confirmation disables it) makes the core log "feedLoopWDT(): Failed to
// feed WDT! Error: 261" on every call, so callers feed only when shouldFeed() says so.
class LoopWdtGate {
 public:
  // Returns true when this call armed the gate, i.e. the caller must enable the watchdog.
  bool arm() { return !armed_.exchange(true); }
  void disarm() { armed_.store(false); }
  bool shouldFeed() const { return armed_.load(); }

 private:
  std::atomic<bool> armed_{false};
};

}  // namespace ota_core

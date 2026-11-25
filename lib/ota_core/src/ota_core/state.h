#pragma once

#include <cstddef>
#include <cstdint>

#include "ota_core/hal.h"

namespace ota_core {

/**
 * The OTA facts that must survive reboots: the anti-rollback floor, the build being
 * verified, builds that failed on this board, and the manifest ETag. Every mutator returns
 * false when the store is unusable, and nothing is written in that case.
 */
class OtaState {
 public:
  static constexpr size_t kBlacklistCapacity = 8;
  static constexpr size_t kEtagMax = 64;

  explicit OtaState(IOtaStore& store) : store_(store) {}

  bool ok() const { return store_.ok(); }

  /** Highest build confirmed good on this board; offers below it are refused. */
  uint32_t floor() const;
  /** Raises the floor, never lowers it. True when the stored floor is at least `build`. */
  bool raiseFloor(uint32_t build);

  /** Build written to the other slot and awaiting its first boot; 0 when none. */
  uint32_t pending() const;
  bool setPending(uint32_t build);
  bool clearPending();

  /** The build that last ran on this board; a change means the image was replaced out of band. */
  uint32_t bootedBuild() const;
  bool setBootedBuild(uint32_t build);

  /** Whether the last verified production manifest listed this board as a release candidate. */
  bool rcListed() const;
  /** Writes only when the value changes, to spare the flash. */
  bool setRcListed(bool listed);

  bool isBlacklisted(uint32_t build) const;
  /** Copies the blacklisted builds, oldest first, and returns how many (at most 8). */
  size_t blacklistEntries(uint32_t out[kBlacklistCapacity]) const;
  /** Idempotent per build; the ninth distinct build evicts the oldest. */
  bool blacklist(uint32_t build);

  /** Copies the stored ETag and returns its length; 0 when none or it does not fit `cap`. */
  size_t etag(char* out, size_t cap) const;
  /** Refuses (false) an ETag longer than kEtagMax rather than truncating it. */
  bool setEtag(const char* etag);
  bool clearEtag();

 private:
  struct Blacklist {
    uint32_t count;
    uint32_t builds[kBlacklistCapacity];
  };
  Blacklist loadBlacklist() const;

  IOtaStore& store_;
};

}  // namespace ota_core

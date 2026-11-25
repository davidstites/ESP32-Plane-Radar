#pragma once

#include <cstddef>
#include <cstdint>

#include "ota_core/hal.h"
#include "ota_core/manifest.h"
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

enum class Outcome : uint8_t { kNotModified, kNoUpdate, kRejected, kFailed, kInstalled };

struct CheckResult {
  Outcome outcome;
  /**
   * Why: "not_modified", "installed", a verdictName or sigErrorName, "fetch_failed",
   * "malformed", "bad_path", "pending_verify", "backoff", "store_unavailable", "begin_failed", "write_failed",
   * "download_incomplete", "hash_mismatch" or "end_failed". Always a string literal.
   */
  const char* reason;
  uint32_t build;  // the offered build once its manifest verified, else 0
  bool candidate;  // the installed image came from this board's release-candidate manifest
};

class Updater {
 public:
    Updater(const Config& cfg, IOtaStore& store, IOtaSlots& slots, IOtaHttp& http)
      : cfg_(cfg), state_(store), slots_(slots), http_(http) {}

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

  /**
   * One scheduled check: fetch the production manifest, verify it, decide, and install into
   * the other slot if it is a newer build. When that manifest lists this board's hash, the
   * board's release-candidate manifest is fetched too and wins if it is strictly newer; a bad
   * or missing candidate never blocks a production install. The ETag is remembered only once
   * a signature verified and the outcome is final, so a failed or forged response is always
   * retried. Never raises the floor.
   *
   * The updater keeps its manifest and read buffers as members (about 4 KB) so a caller with
   * a small task stack must allocate the Updater statically or on the heap.
   */
  CheckResult check();

 private:
  static constexpr size_t kChunkBytes = 2048;

  Config cfg_;
  OtaState state_;
  IOtaSlots& slots_;
  IOtaHttp& http_;
  ManifestResponse response_;
  Manifest manifest_;
  uint8_t chunk_[kChunkBytes];

  CheckResult install(const uint8_t sha[kShaBytes], uint32_t build, size_t length);
  CheckResult fetchAndInstall(const char* firmware, const uint8_t sha[kShaBytes], uint32_t build,
                              bool candidate);
  bool fetchCandidate(const char* ownHash, VerifiedOffer& offer, char* firmware);
  void rememberEtag(const char* etag);

  // A server-side bad image (hash_mismatch, begin_failed) would otherwise be re-downloaded on
  // every 30-minute retry. After two in a row for the same signed hash, skip the next few.
  static constexpr uint8_t kBackoffAfter = 2;
  static constexpr uint8_t kBackoffSkips = 8;
  bool manifestPath(char* path, size_t cap) const;
  uint8_t failSha_[kShaBytes] = {};
  uint8_t failCount_ = 0;
  uint8_t skipsLeft_ = 0;
};

}  // namespace ota_core

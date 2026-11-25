#pragma once

#include <cstddef>
#include <cstdint>

namespace ota_core {

/** Builds above this are treated as a corrupt or hostile manifest, not a real release. */
constexpr uint32_t kMaxSaneBuild = 999999;

/** Why an offer was or was not taken. Names are logged and shown on the status page. */
enum class Verdict : uint8_t {
  kAccept,
  kSuspiciousBuild,
  kBlacklisted,
  kBelowMinBuild,
  kServerRollback,
  kAlreadyCurrent,
  kNotInRollout,
};

const char* verdictName(Verdict v);

struct Offer {
  uint32_t build;
  uint32_t minBuild;
};

/** What this board knows about itself. `floor` only ever rises, at confirm-boot. */
struct LocalState {
  uint32_t build;
  uint32_t floor;
  bool blacklisted;  // the offered build has failed on this board before
  bool inRollout;
};

/** First matching rule wins: suspicious, blacklisted, below floor, rollback, current, rollout. */
Verdict evaluateOffer(const Offer& offer, const LocalState& local);

/** Stable per-board bucket: ((id[0] << 8) | id[1]) % 100 < rollout. 0 never, 100 or more always. */
bool inRolloutGroup(const uint8_t deviceId[32], uint8_t rollout);

/** Same-host absolute path only: one leading '/', 2-96 chars of [A-Za-z0-9._/-], no "//" or "..". */
bool isAllowedPath(const char* path);

/** "<basePath>/rc/<hashHex>.json"; false if hashHex is not 32 lowercase hex or the buffer is small. */
bool formatRcPath(const char* basePath, const char* hashHex, char* out, size_t cap);

/** Exact match of this board's hash against the manifest's candidate list. */
bool isListed(const char* const* entries, size_t count, const char* ownHashHex);

enum class InstallChoice : uint8_t { kNone, kProduction, kCandidate };

/** A candidate wins only if accepted and strictly newer than an accepted production build. */
InstallChoice chooseInstall(bool prodAccepted, uint32_t prodBuild, bool rcAccepted, uint32_t rcBuild);

}  // namespace ota_core

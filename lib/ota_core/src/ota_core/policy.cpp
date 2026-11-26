#include "ota_core/policy.h"

#include <cstdio>
#include <cstring>

namespace ota_core {

namespace {

constexpr size_t kMinPath = 2;
constexpr size_t kMaxPath = 96;
constexpr size_t kHashChars = 32;

bool pathChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
         c == '_' || c == '/' || c == '-';
}

bool lowerHex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }

}  // namespace

const char* verdictName(Verdict v) {
  switch (v) {
    case Verdict::kAccept: return "accept";
    case Verdict::kSuspiciousBuild: return "suspicious_build";
    case Verdict::kBlacklisted: return "blacklisted";
    case Verdict::kBelowMinBuild: return "below_min_build";
    case Verdict::kServerRollback: return "server_rollback";
    case Verdict::kAlreadyCurrent: return "already_current";
    case Verdict::kNotInRollout: return "not_in_rollout";
  }
  return "unknown";
}

Verdict evaluateOffer(const Offer& offer, const LocalState& local) {
  if (offer.build > kMaxSaneBuild) return Verdict::kSuspiciousBuild;
  if (local.blacklisted) return Verdict::kBlacklisted;
  const uint32_t effectiveFloor = offer.minBuild > local.floor ? offer.minBuild : local.floor;
  if (offer.build < effectiveFloor) return Verdict::kBelowMinBuild;
  if (offer.build < local.build) return Verdict::kServerRollback;
  if (offer.build == local.build) return Verdict::kAlreadyCurrent;
  if (!local.inRollout) return Verdict::kNotInRollout;
  return Verdict::kAccept;
}

bool inRolloutGroup(const uint8_t deviceId[32], uint8_t rollout) {
  if (rollout >= 100) return true;
  const uint32_t bucket = ((static_cast<uint32_t>(deviceId[0]) << 8) | deviceId[1]) % 100;
  return bucket < rollout;
}

bool isAllowedPath(const char* path) {
  if (path == nullptr) return false;
  const size_t len = strlen(path);
  if (len < kMinPath || len > kMaxPath || path[0] != '/') return false;
  size_t componentStart = 1;
  for (size_t i = 1; i <= len; ++i) {
    if (i < len && !pathChar(path[i])) return false;
    if (i == len || path[i] == '/') {
      const size_t n = i - componentStart;
      if (n == 0 && i != len) return false;  // "//"
      if (n == 2 && path[componentStart] == '.' && path[componentStart + 1] == '.') return false;
      componentStart = i + 1;
    }
  }
  return true;
}

bool formatRcPath(const char* basePath, const char* hashHex, char* out, size_t cap) {
  if (hashHex == nullptr || strlen(hashHex) != kHashChars) return false;
  for (size_t i = 0; i < kHashChars; ++i) {
    if (!lowerHex(hashHex[i])) return false;
  }
  const int n = snprintf(out, cap, "%s/rc/%s.json", basePath, hashHex);
  return n > 0 && static_cast<size_t>(n) < cap;
}

bool isListed(const char* const* entries, size_t count, const char* ownHashHex) {
  for (size_t i = 0; i < count; ++i) {
    if (strcmp(entries[i], ownHashHex) == 0) return true;
  }
  return false;
}

InstallChoice chooseInstall(bool prodAccepted, uint32_t prodBuild, bool rcAccepted, uint32_t rcBuild) {
  if (rcAccepted && (!prodAccepted || rcBuild > prodBuild)) return InstallChoice::kCandidate;
  return prodAccepted ? InstallChoice::kProduction : InstallChoice::kNone;
}

}  // namespace ota_core

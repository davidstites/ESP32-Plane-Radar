#include "ota_core/state.h"

#include <cstring>

namespace ota_core {

namespace {
constexpr const char* kFloorKey = "floor";
constexpr const char* kPendingKey = "pending";
constexpr const char* kBlacklistKey = "blacklist";
constexpr const char* kEtagKey = "etag";
constexpr const char* kBootedKey = "booted";
constexpr const char* kRcListedKey = "rclisted";
constexpr const char* kActivatedKey = "activated";
constexpr const char* kSoftKey = "soft";
constexpr const char* kSoftSeenKey = "softseen";
}  // namespace

uint32_t OtaState::floor() const { return store_.getUInt(kFloorKey, 0); }

bool OtaState::raiseFloor(uint32_t build) {
  if (!store_.ok()) return false;
  if (floor() >= build) return true;
  return store_.putUInt(kFloorKey, build);
}

uint32_t OtaState::pending() const { return store_.getUInt(kPendingKey, 0); }

bool OtaState::setPending(uint32_t build) { return store_.ok() && store_.putUInt(kPendingKey, build); }

bool OtaState::clearPending() {
  if (!store_.ok()) return false;
  const bool erased = store_.erase(kPendingKey);
  const bool activatedGone = activated() == 0 || store_.erase(kActivatedKey);
  return erased && activatedGone;
}

uint32_t OtaState::activated() const { return store_.getUInt(kActivatedKey, 0); }

bool OtaState::setActivated(uint32_t build) { return store_.ok() && store_.putUInt(kActivatedKey, build); }

uint32_t OtaState::softFailed() const { return store_.getUInt(kSoftKey, 0); }

bool OtaState::setSoftFailed(uint32_t build) { return store_.ok() && store_.putUInt(kSoftKey, build); }

bool OtaState::clearSoftFailed() {
  return store_.ok() && (softFailed() == 0 || store_.erase(kSoftKey));
}

uint32_t OtaState::softForgiven() const { return store_.getUInt(kSoftSeenKey, 0); }

bool OtaState::setSoftForgiven(uint32_t build) { return store_.ok() && store_.putUInt(kSoftSeenKey, build); }

bool OtaState::clearSoftForgiven() {
  return store_.ok() && (softForgiven() == 0 || store_.erase(kSoftSeenKey));
}

uint32_t OtaState::bootedBuild() const { return store_.getUInt(kBootedKey, 0); }

bool OtaState::setBootedBuild(uint32_t build) { return store_.ok() && store_.putUInt(kBootedKey, build); }

bool OtaState::rcListed() const { return store_.getUInt(kRcListedKey, 0) != 0; }

bool OtaState::setRcListed(bool listed) {
  if (!store_.ok()) return false;
  if (rcListed() == listed) return true;
  return store_.putUInt(kRcListedKey, listed ? 1 : 0);
}

OtaState::Blacklist OtaState::loadBlacklist() const {
  Blacklist list{};
  Blacklist raw{};
  const size_t n = store_.getBytes(kBlacklistKey, &raw, sizeof(raw));
  if (n == sizeof(raw) && raw.count <= kBlacklistCapacity) list = raw;
  return list;
}

bool OtaState::isBlacklisted(uint32_t build) const {
  const Blacklist list = loadBlacklist();
  for (uint32_t i = 0; i < list.count; ++i) {
    if (list.builds[i] == build) return true;
  }
  return false;
}

size_t OtaState::blacklistEntries(uint32_t out[kBlacklistCapacity]) const {
  const Blacklist list = loadBlacklist();
  memcpy(out, list.builds, list.count * sizeof(uint32_t));
  return list.count;
}

bool OtaState::blacklist(uint32_t build) {
  if (!store_.ok()) return false;
  if (isBlacklisted(build)) return true;
  Blacklist list = loadBlacklist();
  if (list.count == kBlacklistCapacity) {
    memmove(list.builds, list.builds + 1, (kBlacklistCapacity - 1) * sizeof(uint32_t));
    --list.count;
  }
  list.builds[list.count++] = build;
  return store_.putBytes(kBlacklistKey, &list, sizeof(list));
}

size_t OtaState::etag(char* out, size_t cap) const {
  if (out == nullptr || cap == 0) return 0;
  char buf[kEtagMax + 1];
  const size_t n = store_.getBytes(kEtagKey, buf, sizeof(buf));
  if (n < 2 || n > cap) {  // absent, empty, or no room for the text plus its NUL
    out[0] = '\0';
    return 0;
  }
  memcpy(out, buf, n);
  return n - 1;
}

bool OtaState::setEtag(const char* etag) {
  if (!store_.ok() || etag == nullptr) return false;
  const size_t len = strlen(etag);
  if (len > kEtagMax) return false;
  return store_.putBytes(kEtagKey, etag, len + 1);
}

bool OtaState::clearEtag() { return store_.ok() && store_.erase(kEtagKey); }

}  // namespace ota_core

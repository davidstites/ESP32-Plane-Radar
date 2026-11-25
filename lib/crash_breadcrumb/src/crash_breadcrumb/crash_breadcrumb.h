#pragma once

#include <cstddef>
#include <cstdint>

namespace crash_breadcrumb {

constexpr uint32_t kMagic = 0x43524142;  // "CRAB"
constexpr size_t kLabelMax = 24;
constexpr size_t kMaxBlacklist = 8;

/**
 * A short "what was I doing" note kept in RTC memory, which survives a software reset but
 * holds garbage after a power-on. The magic and the printable check tell the two apart.
 */
struct Breadcrumb {
  uint32_t magic;
  uint32_t uptimeMs;
  char label[kLabelMax];
};

/** Record a checkpoint. Truncates, always NUL-terminates, and stamps the magic. */
void set(Breadcrumb& b, const char* label, uint32_t uptimeMs);

/**
 * Copy the label out. Returns the length copied; empty (and `out` set to "") when the magic
 * is wrong or any byte before the NUL is not printable ASCII.
 */
size_t copyLabel(const Breadcrumb& b, char* out, size_t cap);

struct Report {
  const char* resetReason;
  const char* checkpoint;
  uint32_t checkpointUptimeMs;
  uint32_t runningBuild;
  uint32_t pendingBuild;
  uint32_t blacklist[kMaxBlacklist];
  size_t blacklistCount;
  uint32_t heapFree;
  uint32_t heapLowest;
  const char* slot;
};

/** The crash report as one JSON object. Returns its length, or 0 when `cap` is too small. */
size_t formatReportJson(const Report& r, char* out, size_t cap);

}  // namespace crash_breadcrumb

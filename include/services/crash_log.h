#pragma once

#include <cstddef>

namespace services::crash_log {

/**
 * Note what the firmware is about to do. Stored in RTC memory (no flash writes), so after a
 * reset the report names the last checkpoint reached before it.
 */
void checkpoint(const char* label);

/** The previous run's last checkpoint, reset reason and update state as JSON; 0 if `cap` is too small. */
size_t reportJson(char* out, size_t cap);

}  // namespace services::crash_log

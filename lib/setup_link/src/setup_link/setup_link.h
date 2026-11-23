#pragma once

#include <cstddef>
#include <cstdint>

namespace setup_link {

constexpr size_t kTokenLen = 6;

/** Fills `out` with kTokenLen unambiguous characters derived from `random`. */
void makeToken(uint32_t random, char (&out)[kTokenLen + 1]);

/** Exact, null-safe comparison that does not exit early on the first mismatch. */
bool tokenMatches(const char* expected, const char* given);

/**
 * Writes "<base>?device=<ip>&token=<token>" and returns its length, or 0 when it does
 * not fit (out[0] is then '\0' if cap > 0).
 */
size_t buildSetupUrl(char* out, size_t cap, const char* base, const char* ip,
                     const char* token);

/** Parses and range-checks a lat/lon pair; outputs are untouched on failure. */
bool parseLatLon(const char* lat_text, const char* lon_text, double* lat, double* lon);

enum class LocationResult { kSaved, kBadToken, kBadCoordinates, kSaveFailed };

using SaveFn = bool (*)(double lat, double lon);

/** Checks the token first, then the coordinates; `save` runs only for a valid request. */
LocationResult handleLocationRequest(const char* expected_token, const char* token,
                                     const char* lat_text, const char* lon_text,
                                     SaveFn save);

}  // namespace setup_link

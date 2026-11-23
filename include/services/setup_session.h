#pragma once

#include <cstddef>

namespace services::setup {

/** Generate this boot's setup code. Call once before Wi-Fi setup. */
void init();

/** The code the hosted setup page must send with a location (also shown on the QR screen). */
const char* token();

/** Full setup-page URL for the current LAN address; false without a link or if it does not fit. */
bool url(char* out, size_t cap);

/**
 * Handles GET /location. Returns the HTTP status (200, 400, 403, 500) and points `message`
 * at a short static text for the response body.
 */
int handleLocation(const char* token, const char* lat, const char* lon,
                   const char** message);

}  // namespace services::setup

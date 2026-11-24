# Location Setup Flow (Firmware) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** After Wi-Fi joins with no saved location, the device shows a QR/short-URL setup screen, accepts `GET /location` with a per-boot token, and then shows the radar; the portal gets dark styling with lat/lon under "Advanced".

**Architecture:** All decision logic (token, URL, coordinate parsing, request checking, QR encoding) lives in a new Arduino-free `lib/setup_link` with host tests at 100% line coverage. Thin Arduino glue in `services/` and `wifi_setup.cpp` calls it. `main.cpp` chooses between QR screen and radar from `services::location::hasSaved()`.

**Tech Stack:** PlatformIO (`supermini`, `native`), GoogleTest, WiFiManager 2.0.17, LovyanGFX, `ricmoo/QRCode`.

**Spec:** `docs/superpowers/specs/2026-11-23-location-setup-design.md`

## Global Constraints

- Branch `feature/location-setup-flow`; PR into `main`; never push to `main`. Use `--repo davidstites/ESP32-Plane-Radar` with `gh`.
- Host tests: `scripts/coverage.sh` must pass (100% line coverage of `lib/`). No OCMock; fakes are function pointers or subclass-with-blocks.
- Warning-free builds for both `pio run -e supermini` and `pio test -e native`. Never pipe `pio` output through filters; redirect to a file, then grep the file.
- Heap: ~30 KB free at steady state. Keep QR buffers `static`; loop task stack headroom is ~4 KB. Portal CSS at most 3 KB.
- Default location stays 52.3676, 4.9041 (`config.h`). No skip option on the QR screen.
- Setup page URL: `https://metarly.app/setup`, short form `metarly.app/setup`, in `config.h`.
- Token: 6 characters from `23456789ABCDEFGHJKMNPQRSTUVWXYZ`, generated per boot from `esp_random()`.
- Match the surrounding style: `namespace`/`constexpr`, `snake_case` members with trailing `_` in classes, Doxygen `/** */` comments sparingly.

## Review Focus

- `/location` with missing/empty token, or a token that is a prefix or extension of the real one: reject (403), save nothing.
- `lat`/`lon` that are empty, `nan`, `inf`, `37.7abc`, out of range (91, 181): reject (400), save nothing.
- URL buffer too small for the IP/token: report failure instead of emitting a truncated URL or QR.
- QR payload longer than the largest supported version: encode fails and the screen falls back to text only.
- A first-time Wi-Fi save from the AP portal must not persist the prefilled default location (otherwise `hasSaved()` is true and the QR screen never shows).
- Upgrading devices that already stored lat/lon keep working: `hasSaved()` is true and they boot straight to the radar.

---

### Task 1: `lib/setup_link` core (token, URL, coordinates, request check)

**Files:**
- Create: `lib/setup_link/library.json`, `lib/setup_link/src/setup_link/setup_link.h`, `lib/setup_link/src/setup_link/setup_link.cpp`
- Test: `test/test_setup_link/test_main.cpp`

**Interfaces:**
- Produces (namespace `setup_link`):
  - `constexpr size_t kTokenLen = 6;`
  - `void makeToken(uint32_t random, char (&out)[kTokenLen + 1]);` base-31 digits of `random % 31^6`, most significant first, over the alphabet above.
  - `bool tokenMatches(const char* expected, const char* given);` false if either is null or lengths differ; compares all bytes without early exit.
  - `size_t buildSetupUrl(char* out, size_t cap, const char* base, const char* ip, const char* token);` writes `<base>?device=<ip>&token=<token>`, returns its length, or 0 (and `out[0] = '\0'` when `cap > 0`) if it does not fit.
  - `bool parseLatLon(const char* lat_text, const char* lon_text, double* lat, double* lon);` false on null/empty, trailing junk, NaN/inf, or outside [-90, 90] / [-180, 180]; outputs untouched on failure.
  - `enum class LocationResult { kSaved, kBadToken, kBadCoordinates, kSaveFailed };`
  - `using SaveFn = bool (*)(double lat, double lon);`
  - `LocationResult handleLocationRequest(const char* expected_token, const char* token, const char* lat_text, const char* lon_text, SaveFn save);` token is checked first; `save` is called only for a valid token and valid coordinates; `kSaveFailed` if it returns false.

- [ ] **Step 1: Write failing tests** in `test/test_setup_link/test_main.cpp` (copy the `RadarGeoTest` includes/style):
  - `TokenTest.UsesOnlyUnambiguousAlphabet`: loop `makeToken(i * 2654435761u, t)` for 1000 values; every char is in the alphabet, `strlen == 6`.
  - `TokenTest.IsDeterministicAndDistinct`: same input gives the same token; `makeToken(0)` is `"222222"`; `makeToken(1)` is `"222223"`; `makeToken(31)` is `"222232"`.
  - `TokenTest.MatchRejectsNullPrefixExtensionAndCase`: `tokenMatches("ABCDEF", "ABCDEF")` true; false for `nullptr` on either side, `"ABC"`, `"ABCDEFG"`, `"abcdef"`, `""`.
  - `BuildUrlTest.FormatsQuery`: `buildSetupUrl(buf, 128, "https://metarly.app/setup", "192.168.1.50", "ABCDEF")` returns `strlen(buf)` and equals `https://metarly.app/setup?device=192.168.1.50&token=ABCDEF`.
  - `BuildUrlTest.FailsWhenTooSmall`: cap equal to the exact length (no room for NUL) returns 0 and `buf[0] == '\0'`; cap 0 returns 0 without writing; cap equal to length + 1 succeeds.
  - `ParseLatLonTest`: accepts `"37.7749","-122.4194"`; accepts `"0","0"`, `"90","180"`, `"-90","-180"`; rejects `nullptr`, `""`, `"abc"`, `"37.7abc"`, `"nan"`, `"inf"`, `"91","0"`, `"0","181"`, and a bad lon with a good lat; on failure the outputs keep their sentinel values.
  - `HandleLocationTest` with a file-static `g_save_calls`, `g_saved_lat`, `g_saved_lon`, `g_save_result` and `bool fakeSave(double, double)`: good request returns `kSaved`, one call with 37.7749/-122.4194; wrong token returns `kBadToken` with zero calls even when coordinates are bad; null token `kBadToken`; bad coordinates `kBadCoordinates` with zero calls; `g_save_result = false` returns `kSaveFailed`.

- [ ] **Step 2: Run `pio test -e native -f test_setup_link`** (redirect output to a file in the scratchpad directory, then grep it) and confirm the build fails on the missing header.

- [ ] **Step 3: Implement** `library.json` (copy `lib/net_schedule/library.json`, name `setup_link`, description "Arduino-free setup token, URL, coordinate parsing and /location request checking") and the header/source with the signatures above. `parseLatLon` uses `strtod` + end-pointer check + `std::isfinite` + range check.

- [ ] **Step 4: Run `pio test -e native -f test_setup_link`**; expect all PASS, no warnings.

- [ ] **Step 5: Commit** `git add lib/setup_link test/test_setup_link` with message `Add setup_link: token, setup URL, coordinate parsing, /location request check`.

### Task 2: QR encoding in `lib/setup_link`

**Files:**
- Modify: `platformio.ini` (add `ricmoo/QRCode@^0.0.1` to `lib_deps` of both `[env:supermini]` and `[env:native]`), `lib/setup_link/src/setup_link/setup_link.h`, `.../setup_link.cpp`, `lib/setup_link/library.json`
- Test: `test/test_setup_link/test_main.cpp`

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces:
  - `constexpr uint8_t kQrMaxVersion = 5;` (37x37 modules; 106 bytes at ECC low)
  - `struct QrCode { uint8_t size = 0; bool module(uint8_t x, uint8_t y) const; uint8_t data[...]; }` fixed storage sized for `kQrMaxVersion`, no heap.
  - `bool encodeQr(const char* text, QrCode* out);` smallest version 1..`kQrMaxVersion` that fits, ECC low; false (and `out->size == 0`) if null, empty or too long.

- [ ] **Step 1: Write failing tests**: `QrTest.EncodesShortTextAtVersion1` (`"HI"` gives size 21); `QrTest.SelectsLargerVersionForLongerText` (a 58-character URL gives size 33 (version 4)); `QrTest.FinderPatternCorners` (module(0,0), module(6,0), module(0,6) dark; module(7,7) light); `QrTest.RejectsTooLongNullAndEmpty` (200 chars, `nullptr`, `""` all return false with `size == 0`); `QrTest.ModuleOutOfRangeIsLight`. If `qrcode_initText` does not return an error for oversize input in the installed version, assert on whatever the library does and have `encodeQr` check the length against capacity itself.

- [ ] **Step 2: Run** `pio test -e native -f test_setup_link`; confirm failure (missing `encodeQr`).

- [ ] **Step 3: Implement** by wrapping `qrcode_initText` / `qrcode_getModule` from `<qrcode.h>`; copy modules into `QrCode::data` as a packed bit array so `QrCode` does not point into library state.

- [ ] **Step 4: Run** `pio test -e native -f test_setup_link`; expect PASS. Then run `scripts/coverage.sh` and confirm 100% line coverage of `lib/` (add tests for any uncovered branch).

- [ ] **Step 5: Commit** `Add QR encoding to setup_link`.

### Task 3: `hasSaved()` and the portal-prefill fix

**Files:**
- Modify: `include/services/radar_location.h`, `src/services/radar_location.cpp`, `src/services/wifi_setup.cpp:42-104`

**Interfaces:**
- Consumes: `setup_link::parseLatLon` (Task 1).
- Produces: `bool services::location::hasSaved();` true once a valid lat/lon pair has been loaded from or written to NVS this boot; false after `clear()`.

- [ ] **Step 1: Replace** the duplicated `parseCoord`/`validLatLon` in `radar_location.cpp` with `setup_link::parseLatLon` (DRY); keep `validLatLon` only where `init()` needs it for stored values, or route that through `parseLatLon` on formatted text if simpler. Behavior of `saveFromStrings` must not change.

- [ ] **Step 2: Add `static bool s_saved`**, set true in `init()` when both keys exist and are valid, in `persist()`; false in `clear()`. Implement `hasSaved()` returning it.

- [ ] **Step 3: In `onPortalParamsSaved()`**, keep copies of the lat/lon prefill strings (same pattern as `s_range_prefill`) and call `saveFromStrings` only if either value differs from its prefill. Without this a Wi-Fi save persists Amsterdam.

- [ ] **Step 4: Build** `pio run -e supermini` with output redirected to a scratchpad file; grep the file for `warning` and `error`; expect none.

- [ ] **Step 5: Commit** `Track whether a location was saved; stop persisting the prefilled default`.

### Task 4: Setup session, `/location` endpoint, portal link and Advanced section

**Files:**
- Create: `include/services/setup_session.h`, `src/services/setup_session.cpp`
- Modify: `include/config.h`, `src/services/wifi_setup.cpp`, `src/main.cpp`

**Interfaces:**
- Consumes: `setup_link::makeToken/buildSetupUrl/handleLocationRequest/tokenMatches`, `services::location::saveFromStrings`-equivalent save.
- Produces (namespace `services::setup`):
  - `void init();` generates the token from `esp_random()`; call once in `setup()` before `wifiSetupConnect()`.
  - `const char* token();`
  - `bool url(char* out, size_t cap);` uses `config::kSetupPageUrl` and `WiFi.localIP()`; false without a link or on truncation.
  - `int handleLocation(const char* token, const char* lat, const char* lon, const char** message);` maps `LocationResult` to HTTP status (200, 403, 400, 500) plus a short message; the save callback calls the location service's save with doubles.
- `config.h` additions: `kSetupPageUrl = "https://metarly.app/setup"`, `kSetupPageShortUrl = "metarly.app/setup"`.

- [ ] **Step 1: Implement `setup_session`** as above. The save callback needs a double-based save: add `bool services::location::save(double lat, double lon);` (validates range, persists, returns false on invalid) and make `saveFromStrings` call it.

- [ ] **Step 2: Register the route** with `s_wm.setWebServerCallback(...)` in `ensureWifiManager()`; inside, `s_wm.server->on("/location", HTTP_GET, ...)` reads args `lat`, `lon`, `token`, calls `services::setup::handleLocation`, and replies with the status and a minimal HTML body (`Location saved. You can close this tab.` or the error message). The handler runs on the loop task inside `s_wm.process()`, so no extra locking is needed.

- [ ] **Step 3: Advanced section**: wrap the lat/lon parameters between two HTML-only `WiFiManagerParameter` objects (`<details><summary>Advanced: coordinates</summary>` and `</details>`) in `attachPortalParams`, and add one HTML-only parameter at the top holding a `static char` buffer refreshed in `refreshPortalParamValues()` with `<p><a href="URL">Set location with address search</a></p>` when `services::setup::url` succeeds, else an empty string.

- [ ] **Step 4: Build** `pio run -e supermini`; grep the output file for warnings/errors; expect none.

- [ ] **Step 5: Commit** `Add /location endpoint, setup token and portal location link`.

### Task 5: Portal CSS

**Files:**
- Create: `include/services/portal_style.h`
- Modify: `src/services/wifi_setup.cpp` (`ensureWifiManager`: `s_wm.setCustomHeadElement(kPortalHeadHtml)`), `include/config.h` only if a constant is needed

**Interfaces:**
- Produces: `constexpr char kPortalHeadHtml[]` containing one `<style>` block, at most 3 KB, dark background, radar-green accent, large tap targets, system fonts, `details/summary` styling, no external resources.

- [ ] **Step 1: Write the CSS** targeting the WiFiManager classes (`body`, `.c`, `button`, `input`, `a`, `.msg`, `details`). Add a `static_assert(sizeof(kPortalHeadHtml) <= 3072, "portal CSS over budget")`.

- [ ] **Step 2: Build** `pio run -e supermini`; expect no warnings.

- [ ] **Step 3: Commit** `Style the setup portal`.

### Task 6: QR status screen and state wiring

**Files:**
- Modify: `include/ui/status_screens.h`, `src/ui/status_screens.cpp`, `src/main.cpp`

**Interfaces:**
- Consumes: `setup_link::encodeQr`, `services::setup::url/token`, `services::location::hasSaved`, `config::kSetupPageShortUrl`.
- Produces: `void statusScreenSetupQr(const char* url, const char* ip, const char* token);` white background, QR at 4 px per module centered in the upper part, then `metarly.app/setup` and `<ip>  <token>` in small text within the round panel; falls back to text-only (short URL, IP, token) if `encodeQr` fails.

- [ ] **Step 1: Implement `statusScreenSetupQr`** using a `static setup_link::QrCode` and `tft.fillRect` per dark module (QR dark on white; the white background supplies the quiet zone).

- [ ] **Step 2: Change `showRadarIfConnected()`** in `main.cpp`: return with `g_radar_visible = false` when Wi-Fi is down; when `!services::location::hasSaved()`, draw the QR screen once (track with a `static bool` reset when the link drops) and return; otherwise draw the radar as before. Call `services::setup::init()` before `wifiSetupConnect()`.

- [ ] **Step 3: Confirm the transition**: after `/location` saves, the next `loop()` pass sees `hasSaved()` true and draws the radar (the `!g_radar_visible` branch already redraws).

- [ ] **Step 4: Build** `pio run -e supermini`; expect no warnings.

- [ ] **Step 5: Commit** `Show a setup QR screen until a location is saved`.

### Task 7: Docs, full verification, PR

**Files:**
- Modify: `README.md` (setup section: new flow, `/location` contract, "Advanced" fields, `kSetupPageUrl`)

- [ ] **Step 1: Update README** (Wi-Fi setup portal and Configuration sections).
- [ ] **Step 2: Run `scripts/coverage.sh`** (output to a file); expect pass at 100%.
- [ ] **Step 3: Run `pio run -e supermini`** (output to a file); expect no warnings.
- [ ] **Step 4 (hardware, needs David's go-ahead to flash):** flash to `/dev/cu.usbmodem101`, boot with location cleared, confirm the QR screen, request `http://<ip>/location?lat=37.7749&lon=-122.4194&token=<code>` with curl, confirm 200 and the radar appears; confirm wrong token returns 403; read `feeds:` heap log line and record free heap with the portal active.
- [ ] **Step 5: Commit, push `feature/location-setup-flow`, open a PR** with `gh pr create --repo davidstites/ESP32-Plane-Radar` and the attribution lines from the session reminder.

# Location Setup Page (`site/`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A static page at `https://metarly.app/setup` where a user picks a location (airport code, address, current location or coordinates) and hands it to their radar with one tap.

**Architecture:** No build step and no dependencies: plain ES modules under `site/setup/lib/` hold all logic and are unit tested with `node:test` at 100% line coverage; `app.js` only wires them to the DOM. Airport lookup uses a generated `airports.json`; addresses go straight from the browser to Nominatim (Photon fallback). The page hands off by navigating (not `fetch`) to `http://<device>/location?...`.

**Tech Stack:** HTML/CSS/ES modules, `node:test` (Node 22+), Python 3 (`unittest`) for the data script.

**Spec:** `docs/superpowers/specs/2026-11-23-location-setup-design.md` (sections "Why a hosted page", "Device endpoint", "Page"). Firmware plan: `docs/superpowers/plans/2026-11-23-location-setup-firmware.md`.

## Global Constraints

- Branch `feature/location-setup-site`, stacked on `feature/location-setup-flow` (the spec and firmware live there). Never push to `main`; `gh` needs `--repo davidstites/ESP32-Plane-Radar`.
- Page URL path is `/setup`: files live in `site/setup/` (`index.html` served at `/setup/`; the host must redirect `/setup` to `/setup/`). Nothing outside `site/` is deployed.
- Device contract (firmware, already built): `GET http://<host>/location?lat=<deg>&lon=<deg>&token=<CODE>`. Replies 200 saved, 403 wrong/missing code, 400 bad coordinates. Code: 6 characters from `23456789ABCDEFGHJKMNPQRSTUVWXYZ`; the device compares case-insensitively.
- `device` is accepted only for `*.local` hostnames, RFC 1918 IPv4 (10/8, 172.16/12, 192.168/16) and link-local IPv4 (169.254/16). Bare host only: no scheme, port, path, userinfo or IPv6.
- No third-party code, no CDN, no web fonts. Content-Security-Policy meta: `default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self' https://nominatim.openstreetmap.org https://photon.komoot.io; img-src 'self'; base-uri 'none'; form-action 'none'`.
- Geocoding only on an explicit button press (Nominatim public-server policy: no autocomplete). Browser sends the Referer automatically; do not suppress it.
- Untrusted text (geocoder labels, query params) goes into the DOM with `textContent`, never `innerHTML`.
- Palette shared with the portal (`include/services/portal_style.h`): bg `#0a0f0c`, panel `#121b16`, border `#2a3d32`, text `#d7f5e3`, muted `#8aa196`, accent `#34d27b`, danger `#e5534b`. Dark only (`color-scheme: dark`), system fonts, mobile first (16 px gutters, 44 px tap targets).
- Tests: `node --test --experimental-test-coverage --test-coverage-lines=100 --test-coverage-include='site/setup/lib/*.js' site/tests/` and `python3 -m unittest discover -s scripts/tests -v` both pass.

## Review Focus

- `device` tricks: `evil.com`, `192.168.1.1@evil.com`, `http://192.168.1.1`, `192.168.1.1:80`, `[::1]`, `10.0.0.1.evil.com`, `172.32.0.1` (outside /12), `256.1.1.1`, `localhost`, `192.168.01.1` (leading zero) must all be rejected; `PLANE-RADAR.LOCAL` and ` 192.168.5.164 ` (whitespace) accepted and normalised.
- Pasted coordinates `37.7749, -122.4194`, `37.7749 -122.4194`, and swapped/out-of-range values (`-122.4194, 37.7749`) give a clear message, never a hand-off.
- Code typed as `abc 234`, `abc-234`, lowercase, or with the ambiguous characters `0 O 1 I L`: normalised or rejected with a hint, never sent malformed.
- Both geocoders failing (offline, 429), an empty result set, a whitespace-only query and a 500-character query: a visible message and the coordinates fallback, no thrown error.
- Airport queries `ksfo`, `SFO`, `san francisco`, `KSFO ` and a code that is both an IATA code and a word: the exact code match ranks first.
- Geolocation denied, unavailable, timed out, or very inaccurate (> 5 km): message plus fallback; the inaccurate fix is flagged as approximate.
- A geocoder label containing `<img src=x onerror=alert(1)>` renders as text.
- A city-level or postcode-level match is visibly marked approximate before sending.
- The one-time code is removed from the address bar (`history.replaceState`) after being read, and is still sent.

---

### Task 1: Airport data script

**Files:**
- Create: `scripts/build_airports_json.py`, `scripts/tests/test_build_airports_json.py`, `site/setup/data/airports.json` (generated, committed)

**Interfaces:**
- Consumes: `build_large_airports.fetch_csv(url) -> list[dict[str, str]]` and `AIRPORTS_URL`.
- Produces: `site/setup/data/airports.json`, a JSON array of `[ident, iata, name, lat, lon]` (strings, string, string, number, number; `iata` is `""` when absent; lat/lon rounded to 5 decimals), sorted by ident. Python functions `airport_rows(rows: list[dict[str, str]]) -> list[list]` (pure filter/shape) and `main() -> None`.

- [ ] **Step 1: Write failing tests** in `scripts/tests/test_build_airports_json.py` (import with `sys.path` like `test_check_ca_bundle.py`): `test_keeps_icao_style_ident` (a `small_airport` row with ident `KHAF`, no IATA is kept); `test_keeps_row_with_iata_even_if_ident_not_four_letters` (`ident` `CA23`... only if `iata_code` is `ABC`); `test_drops_non_airport_types` (`heliport`, `closed`, `seaplane_base`, `balloonport`); `test_drops_non_icao_without_iata` (`ident` `CA23`, no IATA); `test_drops_rows_without_coordinates`; `test_rounds_to_five_decimals_and_sorts_by_ident`; `test_shape_is_ident_iata_name_lat_lon`.
- [ ] **Step 2: Run** `python3 -m unittest scripts.tests.test_build_airports_json -v`; expect FAIL (module missing).
- [ ] **Step 3: Implement `airport_rows`** keeping types `large_airport`, `medium_airport`, `small_airport` that have coordinates and either an `iata_code` or an all-letters 4-character `ident`; `main()` fetches with `fetch_csv(AIRPORTS_URL)`, writes compact JSON (`separators=(",", ":")`, `ensure_ascii=False`) to `site/setup/data/airports.json`.
- [ ] **Step 4: Run the tests**; expect PASS. Then run `python3 scripts/build_airports_json.py` and check the file has more than 5,000 entries and is under 1.5 MB.
- [ ] **Step 5: Commit** `Add airports.json build script and data`.

### Task 2: Input validation and hand-off URL

**Files:**
- Create: `site/setup/lib/device.js`, `site/setup/lib/coords.js`, `site/setup/lib/handoff.js`
- Test: `site/tests/device.test.js`, `site/tests/coords.test.js`, `site/tests/handoff.test.js`

**Interfaces:**
- Produces (all ES module named exports):
  - `device.js`: `parseDevice(input: string): string | null` returns the normalised host (trimmed, lower-cased) or `null`; `parseToken(input: string): string | null` strips spaces and hyphens, upper-cases, and returns the 6-character code or `null` if the length or alphabet is wrong; `TOKEN_ALPHABET: string`.
  - `coords.js`: `parseLatLon(input: string): { lat: number, lon: number } | { error: string }` accepts `"lat, lon"` or `"lat lon"` (decimal degrees); `error` is a user-readable sentence (not a number, out of range, looks swapped).
  - `handoff.js`: `buildHandoffUrl(host: string, lat: number, lon: number, token: string): string` returns `http://<host>/location?lat=<6 decimals>&lon=<6 decimals>&token=<CODE>` and throws `Error` if `host` fails `parseDevice`, the token fails `parseToken` or the coordinates are out of range.

- [ ] **Step 1: Write failing tests**: `device.test.js` cases from Review Focus (each rejected input asserts `null`; `PLANE-RADAR.LOCAL` → `plane-radar.local`; `' 192.168.5.164 '` → `192.168.5.164`; `10.1.2.3`, `172.16.0.1`, `172.31.255.255`, `169.254.1.1` accepted); token cases (`abc 234` → `ABC234`, `abc-234`, lowercase, length 5 and 7 → `null`, a code containing `0`, `O`, `1`, `I` or `L` → `null`). `coords.test.js`: `"37.7749, -122.4194"`, `"37.7749 -122.4194"`, `"  37.7749,-122.4194  "` parse; `"-122.4194, 37.7749"` returns an error mentioning latitude; `"abc"`, `""`, `"91, 0"`, `"0, 181"`, `"1,2,3"` return errors. `handoff.test.js`: exact URL for `("192.168.5.164", 37.1476, -121.6729, "7xsyty")` is `http://192.168.5.164/location?lat=37.147600&lon=-121.672900&token=7XSYTY`; throws for host `evil.com`, a bad token and `lat = 91`.
- [ ] **Step 2: Run** `node --test site/tests/`; expect FAIL (modules missing).
- [ ] **Step 3: Implement** the three modules with the signatures above. `parseDevice`: `.local` names match `^[a-z0-9]([a-z0-9-]*[a-z0-9])?(\.[a-z0-9]([a-z0-9-]*[a-z0-9])?)*\.local$`; IPv4 needs four dotted decimal octets 0-255 with no leading zeros, then the range check.
- [ ] **Step 4: Run** the tests with the coverage flags from Global Constraints; expect PASS and 100% lines.
- [ ] **Step 5: Commit** `Add device, coordinate and hand-off validation for the setup page`.

### Task 3: Airport search

**Files:**
- Create: `site/setup/lib/airports.js`
- Test: `site/tests/airports.test.js`

**Interfaces:**
- Consumes: the array format from Task 1.
- Produces: `searchAirports(index: Array<[string, string, string, number, number]>, query: string, limit?: number): Array<{ ident: string, iata: string, name: string, lat: number, lon: number, label: string }>` with `limit` default 5. Ranking: exact ident, exact IATA, ident prefix, then name substring (case-insensitive, whitespace-trimmed); an empty or 1-character query returns `[]`; `label` is `"KSFO · SFO · San Francisco International Airport"` (parts omitted when empty).

- [ ] **Step 1: Write failing tests** with a 6-row fixture: `ksfo` and ` KSFO ` find KSFO first; `SFO` finds KSFO via IATA; `san francisco` finds it by name; exact code outranks a name hit (fixture has a row whose name contains `sfo`); prefix `KSF` lists KSFO; `limit` truncates; empty and 1-character queries return `[]`; no match returns `[]`; `label` format with and without IATA.
- [ ] **Step 2: Run** `node --test site/tests/airports.test.js`; expect FAIL.
- [ ] **Step 3: Implement** `searchAirports` with a single pass that buckets rows by rank, then concatenates and slices.
- [ ] **Step 4: Run** with coverage flags; expect PASS, 100%.
- [ ] **Step 5: Commit** `Add airport search for the setup page`.

### Task 4: Geocoding

**Files:**
- Create: `site/setup/lib/geocode.js`
- Test: `site/tests/geocode.test.js`

**Interfaces:**
- Produces:
  - `type Precision = "building" | "street" | "postcode" | "area"`; `type Place = { lat: number, lon: number, label: string, precision: Precision }`.
  - `parseNominatim(json: unknown): Place[]` from a `jsonv2` search array (`lat`/`lon` are strings; `display_name`; precision from `place_rank`: ≥ 28 building, 26-27 street, 22-25 postcode, else area).
  - `parsePhoton(json: unknown): Place[]` from GeoJSON (`geometry.coordinates` is `[lon, lat]`; label joins `name`, `street`, `housenumber`, `city`, `country` that exist; precision from `properties.type`: `house` building, `street` street, `postcode` postcode, anything else area).
  - `geocode(query: string, fetchImpl?: typeof fetch): Promise<{ places: Place[], source: "nominatim" | "photon" } | { error: string }>`: a trimmed query shorter than 3 or longer than 200 characters returns `{ error }` without calling `fetchImpl`; tries Nominatim (`https://nominatim.openstreetmap.org/search?format=jsonv2&limit=5&q=<encoded>`), falls back to Photon (`https://photon.komoot.io/api/?limit=5&q=<encoded>`) on a network error, a non-OK status or an empty result; both failing yields `{ error }`.

- [ ] **Step 1: Write failing tests** with fixtures: Nominatim house/road/postcode/city rows map to the four precisions; non-array or malformed rows are skipped, not thrown; Photon `house`/`street`/`postcode`/`city` map likewise and coordinates are swapped correctly; `geocode`: short query, long query and whitespace-only query return `{ error }` with zero fetch calls; Nominatim success returns `source: "nominatim"` with one fetch; Nominatim rejecting, returning status 429, and returning `[]` each fall back to Photon (`source: "photon"`); both failing returns `{ error }`; the request URL contains the encoded query (`"1 Main St & 2"` is percent-encoded).
- [ ] **Step 2: Run** `node --test site/tests/geocode.test.js`; expect FAIL.
- [ ] **Step 3: Implement** the three functions; `geocode` takes `fetchImpl = globalThis.fetch` and never throws.
- [ ] **Step 4: Run** with coverage flags; expect PASS, 100%.
- [ ] **Step 5: Commit** `Add geocoding with Nominatim and Photon fallback`.

### Task 5: The page

**Files:**
- Create: `site/setup/index.html`, `site/setup/style.css`, `site/setup/app.js`, `site/favicon.svg` is NOT needed (omit)
- Modify: none

**Interfaces:**
- Consumes: `parseDevice`, `parseToken` (device.js), `parseLatLon` (coords.js), `buildHandoffUrl` (handoff.js), `searchAirports` (airports.js), `geocode` (geocode.js), `data/airports.json`.
- Produces: the page. Fields and behaviour:
  - **Your radar:** `device` and `code` inputs prefilled from `?device=` and `?token=`; the token is then removed from the address bar with `history.replaceState`; invalid values show an inline message under the field.
  - **Where is it:** one search box ("Airport code, place or address") with a Search button (Enter submits): airport matches (loaded lazily from `data/airports.json` on first search) are listed first, then, only if the query is not an exact airport code, geocoder results; a "Use my current location" button (`navigator.geolocation.getCurrentPosition`, 15 s timeout); an "Enter coordinates" disclosure with one field parsed by `parseLatLon`.
  - **Result list:** each result is a button showing its label and a precision tag (`Exact address`, `Street`, `Postcode`, `Approximate area`, `Airport`, `Your device`); choosing one fills a confirmation panel showing the label, the coordinates to 4 decimals and, for `postcode`/`area` results or a geolocation accuracy over 5 km, an "approximate" warning.
  - **Send to radar** button, enabled only when device, code and a location are all valid; it calls `location.assign(buildHandoffUrl(...))`.
  - **Privacy note:** one sentence that addresses go from the browser to OpenStreetMap's geocoder and the radar only receives coordinates.
  - Palette, CSP meta and tap-target rules from Global Constraints; all dynamic text via `textContent`.

- [ ] **Step 1: Write `index.html`** (the CSP meta from Global Constraints, viewport meta, `<script type="module" src="app.js">`, semantic sections with labels and `aria-live="polite"` message regions) and `style.css` (tokens on `:root`, 16 px gutters, 44 px minimum control height, visible focus ring in the accent colour, `color-scheme: dark`).
- [ ] **Step 2: Write `app.js`** as thin wiring over the lib modules; no logic beyond DOM reads/writes, state held in one `state` object (`device`, `code`, `place`), and a single `render()` that sets the button state and messages.
- [ ] **Step 3: Serve locally** with `python3 -m http.server 8080 --directory site` and, with the Playwright browser tools, open `http://localhost:8080/setup/?device=192.168.5.164&token=abc234`; assert the token field is `ABC234`, the address bar no longer contains `token=`, and Send is disabled until a location is chosen.
- [ ] **Step 4: Browser checks**: search `KSFO` lists the airport first with the `Airport` tag; choosing it enables Send and the button's target (read from the page, do not follow it) is `http://192.168.5.164/location?lat=37.61890&lon=-122.37500&token=ABC234`-style with six decimals; `evil.com` as device keeps Send disabled and shows the inline message; paste `-122.4194, 37.7749` into coordinates shows the swapped-values message; with `fetch` stubbed to reject, an address search shows the failure message and leaves the coordinates fallback usable; stub a geocoder label `<img src=x onerror=alert(1)>` and confirm it appears as text and no dialog fires; resize to 375 px wide and confirm no horizontal scroll.
- [ ] **Step 5: Commit** `Add the setup page`.

### Task 6: CI, docs and PR

**Files:**
- Modify: `.github/workflows/build.yml` (new `site` job), `README.md` (new "Setup page" section)

**Interfaces:** none.

- [ ] **Step 1: Add a `site` job** to `build.yml`: checkout, `actions/setup-node@v4` with `node-version: 22`, then the `node --test ...` command from Global Constraints. The existing Python step already runs `python3 -m unittest discover -s scripts/tests`.
- [ ] **Step 2: README**: describe the page, how to regenerate the airport data (`python3 scripts/build_airports_json.py`), how to run its tests, the `/setup` hosting requirement (deploy the contents of `site/` as the web root; `/setup` must serve `site/setup/index.html`), and the privacy note.
- [ ] **Step 3: Run** both test commands from Global Constraints; expect PASS, 100% line coverage of `site/setup/lib/*.js`.
- [ ] **Step 4: Commit**, then (after David's go-ahead for the new remote branch) push `feature/location-setup-site` and open a PR with `gh pr create --repo davidstites/ESP32-Plane-Radar`, attribution lines from the session reminder, and the deploy steps and the on-device hand-off check as its test plan.

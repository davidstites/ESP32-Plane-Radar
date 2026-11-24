# Plane Radar

<img width="800" height="450" alt="plane-radar" src="https://github.com/user-attachments/assets/716d0992-dab8-47ba-8f1a-2aec7f607419" />

**3D printed case (STL + assembly):** [MakerWorld](https://makerworld.com/en/models/2872376-esp32-plane-radar-live-ads-b-on-a-round-display#profileId-3207083) · **Firmware:** [Releases](https://github.com/MatixYo/ESP32-Plane-Radar/releases)

Firmware for an **ESP32-C3 Super Mini** and a **1.28″ round GC9A01** display (240×240). Shows a circular **ADS-B radar** around your configured location, with **WiFiManager** for first-time setup.

## What it does

1. **Wi‑Fi setup** (if needed) — captive portal on AP **`PlaneRadar-Setup`**
2. **Radar** — live aircraft from [adsb.fi](https://opendata.adsb.fi/) on a sonar-style grid

After Wi‑Fi is saved, the device reconnects automatically; the radar redraws at ~4 Hz with aircraft dead-reckoned along their track, while a background task fetches ADS-B updates (~5 s).

## Controls (BOOT, GPIO 9, active LOW)

| Action | Effect |
|--------|--------|
| **Short tap** | Cycle range preset (5 → 10 → 15 → 25 km); saved to flash |
| **Hold 3 s** | Clear Wi‑Fi, location, and units; reboot into setup portal |

The 3 s hold works any time the firmware is running, including during Wi‑Fi connect and the setup portal. Don't hold BOOT while powering on or pressing reset: GPIO 9 is an ESP32‑C3 strapping pin, so a low level at reset enters the ROM download mode instead of starting the firmware.

## Wi‑Fi setup portal

**First-time setup** (no saved Wi‑Fi):

1. Connect to **`PlaneRadar-Setup`**
2. Open **`http://plane-radar.local`** (preferred) or **`http://192.168.4.1`** — both are shown on the yellow setup screen; captive portal may open automatically
3. Set home Wi‑Fi, then save
4. **Set your location**: once the device joins Wi‑Fi it shows a QR code (and `metarly.app/setup`, the device IP and a 6-character code for typing). Scan it, or open the page and enter the IP and code, then search an address or airport (ICAO/IATA), or tap **Use my current location**. The radar appears as soon as a location is saved. There is no skip: the built-in default location is Amsterdam. Devices that only ever stored that default (older firmware saved it on every portal save) are asked for a location again. The code is not case-sensitive

**Reconfigure anytime** (after the device is on your network):

1. Open **`http://plane-radar.local`** or **`http://<device-ip>`** (e.g. from your router or serial log at boot)
2. **Configure WiFi** changes the network. **Setup** holds units, range and overlays; its **Set location with address search** link reopens the setup page for this device, and raw latitude/longitude are under **Advanced**. Save on each page

The same portal runs on the setup AP and on the device’s LAN IP while connected to Wi‑Fi. mDNS hostname is `plane-radar` → **plane-radar.local** (`kPortalHostname` in `config.h`). Some clients resolve `.local` slowly; use the IP if needed.

**Custom fields** (stored in NVS):

| Field | Purpose |
|-------|---------|
| **Latitude / Longitude** (under *Advanced*) | Radar center and ADS-B query position. Normally set by the hosted setup page; the fields are prefilled with the saved location and are only saved when you change them |
| **Display distances in miles** | Ring scale label in **mi** instead of **km** (e.g. `6mi` vs `10km`) |
| **Show airport runways** | Major-airport runway overlay on the radar (off to hide) |

**Location hand-off** (`GET /location?lat=…&lon=…&token=…`): the hosted page navigates your browser to the device with the setup code from the QR screen. A wrong or missing code returns 403, bad coordinates 400, and nothing is saved. The code is random per boot and stops a stray or mistyped request from changing your radar's center. It is not a security boundary: the portal has no login, and WiFiManager's own `/paramsave` and `/wifisave` pages accept requests from any page on your network without a code.

After a reset, the device reboots and shows the setup screen immediately (no “Connecting” loop on stale credentials).

## Setup page (metarly.app)

`site/setup/` is the static page the QR code opens: it takes a place (airport code, address, your current location or coordinates) and sends it to the radar. It has no build step and no dependencies; deploy the contents of `site/` as the web root so that `/setup` serves `site/setup/index.html`.

- **How it works:** the page reads the radar's address and one-time code from the QR link, then navigates your browser to `http://<radar>/location?lat=…&lon=…&token=…` (a navigation, not a `fetch`, because an HTTPS page may not call a plain-HTTP device). It only accepts `*.local` names and private or link-local addresses as the radar address.
- **Lookups:** airport codes and names are matched in the browser against `site/setup/data/airports.json`. Addresses go from your browser to OpenStreetMap's Nominatim (Photon as a fallback), one request per search press. The radar only receives coordinates.
- **Regenerate the airport list:** `python3 scripts/build_airports_json.py`
- **Tests** (Node 22+, no install): `node --test --experimental-test-coverage --test-coverage-lines=100 --test-coverage-include='site/setup/lib/*.js' 'site/tests/*.test.js'`. The logic lives in `site/setup/lib/`; `app.js` only wires it to the page.
- **Try it locally:** `python3 -m http.server 8080 --directory site`, then open `http://localhost:8080/setup/?device=<radar-ip>&token=<code>`.

## Radar display

### Grid

- Dark blue background, subdued green rings and crosshairs
- White **N / S / E / W** at the bezel; range label on the **east** spoke (ring 3 = ¾ of outer radius)
- White center dot

Layout and colors: `include/ui/radar_theme.h`.

### Range presets

| Ring 3 label | Outer radius (aircraft scale) |
|------------|-------------------------------|
| 5 km / 3 mi | ~6.7 km |
| 10 km / 6 mi | ~13.3 km (default) |
| 15 km / 9 mi | ~20 km |
| 25 km / 16 mi | ~33.3 km |

Preset and miles/km choice persist across reboot (`planeradar` NVS namespace).

### Runways

- Major airports from OurAirports (`large_airport`); all open runway strips in range (helipads excluded)
- Teal runway lines with one ICAO label per airport (e.g. `KJFK`); toggle in the Wi‑Fi setup portal
- A dot beside each label shows the airport's METAR flight category from aviationweather.gov: green VFR, blue MVFR, red IFR, magenta LIFR; no dot when there is no report from the last two hours. Refreshed every 15 minutes (`kFeedRefreshIntervalMs`) and shortly after the range or location changes
- Labels that would overlap are moved just below or above, or dropped (nearest the center wins); the runway lines stay
- Update the embedded list: `python3 scripts/build_large_airports.py`

### Precipitation (US only)

- A faint NEXRAD base-reflectivity layer behind the grid: green for light, yellow for moderate, red for heavy. It is the Iowa Mesonet radar mosaic, so it covers the US only; elsewhere the layer stays empty
- Refreshed every 15 minutes and shortly after the range or location changes; until the image for the new view arrives, nothing is drawn rather than a misplaced layer
- While it shows, only the labeled ring and the outer ring are drawn; the inner rings return when it is off
- Toggle **Show NEXRAD precipitation (US only)** in the Wi‑Fi setup portal (on by default)
- The image is a 120×120 palette PNG (about 0.1-6 KB) fetched over plain HTTP: the radar host's TLS certificate chain does not fit in the ESP32-C3's heap. It is public imagery, decoded by a bounded in-firmware PNG decoder (`lib/nexrad_decode`)

### Aircraft

- **Inside the outer ring** — red heading triangle, magenta speed vector (clipped at the ring), callsign / type / altitude tags
- **Outside the ring** (still within ADS-B fetch) — small **red dot on the screen rim** at the correct bearing (direction cue; not distance-accurate past the ring)
- **Tags** — placed toward the **center**: west (left) → tag on the **right** of the symbol; east (right) → tag on the **left**
- **Tag spacing** — each tag prefers that spot but moves (other side, shifted up/down, above/below) to avoid covering another tag or aircraft, nearest-to-center aircraft first; a tag keeps its spot while it stays free. In a crowd at a wide range some overlap is unavoidable, and the nearer aircraft's tag is drawn on top

As range decreases (or aircraft approach), targets move inward; beyond-ring dots become full symbols when they cross the outer ring.

### ADS-B

- Source: `https://opendata.adsb.fi/api/v3/`
- Fetch radius: `ui::radar::fetchRadiusKm()` — scales with the active preset to roughly the screen edge (so rim dots have data)
- Poll interval: `kAdsbFetchIntervalMs` (5 s) in `config.h`
- Ground aircraft hidden by default (`kAdsbShowGroundAircraft`)
- One network task runs the ADS-B polls and the weather refresh one at a time (`net_schedule`); the weather refresh closes the kept-alive ADS-B connection first, because two TLS sessions do not fit in the C3's heap
- One HTTPS connection is kept alive between polls (HTTP/1.1), so the TLS handshake happens once rather than on every fetch; a dropped connection is re-established automatically
- The response is parsed as it streams in, keeping only the fields the radar uses (`lib/adsb_parse/`)
- The server certificate is verified against the embedded CA bundle `data/x509_crt_bundle.bin` (Mozilla's root list plus `data/ca_extra/`). Certificate dates are not checked: Arduino core 2.0.14 builds mbedTLS without time support
- Refresh the bundle with `scripts/update_ca_bundle.sh`; confirm it still validates adsb.fi with `python3 scripts/check_ca_bundle.py` (needs Python ≥ 3.13 and `cryptography`). The script models how the device's mbedTLS builds the chain; its offline tests run with `python3 -m unittest discover -s scripts/tests`

## Configuration

Edit **`include/config.h`** for hardware and behavior:

| Area | Keys / notes |
|------|----------------|
| Portal | `kPortalApName`, `kPortalIp`, `kPortalHostname` / `kPortalHostUrl` (mDNS; needs `-DWM_MDNS` in `platformio.ini`) |
| Wi‑Fi timing | connect attempts, reconnect grace, portal timeout (`0` = no timeout) |
| BOOT | `kBootPin`, `kBootResetHoldMs`, `kBootTapMinMs` |
| Display SPI | pins, `kDisplayInvert`, `kDisplayRgbOrder`, `kDisplaySpiWriteHz` |
| Default location | `kDefaultRadarLat`, `kDefaultRadarLon` (placeholder, never shown: the QR setup screen appears until a location is saved) |
| Setup page | `kSetupPageUrl` (encoded in the QR), `kSetupPageShortUrl` (shown as text) |
| ADS-B | `kAdsbFetchIntervalMs`, `kAdsbShowGroundAircraft` |

Range presets: `include/ui/radar_range.h` (`kRangePresets`).

## Project layout

```
include/
  config.h
  hardware/
    boot_button.h
    lgfx_config.hpp
    display.h
    display_font.h
  data/
    large_airports.h
  ui/
    radar_theme.h
    radar_range.h
    radar_display.h
    runway_overlay.h
    status_screens.h
  services/
    wifi_setup.h
    radar_location.h
    setup_session.h
    portal_style.h
    adsb_client.h
data/
  ui_font.vlw              — embedded smooth UI font (Noto Sans Bold)
  x509_crt_bundle.bin      — embedded CA bundle for adsb.fi TLS verification
  ca_extra/                — roots added on top of Mozilla's list (see its README)
lib/
  adsb_parse/              — Arduino-free HTTP body decoding + aircraft parsing (unit tested)
  button_logic/            — Arduino-free tap / long-press timing for the BOOT button (unit tested)
  radar_geo/               — Arduino-free lat/lon → km offsets and screen projection (unit tested)
  setup_link/              — Arduino-free setup code, setup URL, coordinate parsing, /location request check and QR encoding (unit tested)
scripts/
  build_large_airports.py
  build_airports_json.py   — airports.json for the setup page
  coverage.sh              — host unit tests + coverage gate
  update_ca_bundle.sh / check_ca_bundle.py
site/
  setup/                   — static location setup page (index.html, app.js, lib/, data/)
  tests/                   — Node tests for site/setup/lib
test/
  fixtures/                — recorded adsb.fi responses
src/
  main.cpp
  data/
    large_airports_data.cpp
  hardware/
  ui/
  services/
```

## Wiring (GC9A01 ↔ ESP32-C3 Super Mini)

| Display | ESP32-C3 |
|---------|----------|
| VCC | 3V3 |
| GND | GND |
| RST | GPIO **0** |
| CS | GPIO **1** |
| DC | GPIO **10** |
| SDA (MOSI) | GPIO **3** |
| SCL (SCLK) | GPIO **4** |
| BOOT (user) | GPIO **9** |

## Build

```bash
pio run -t upload
pio device monitor
```

- PlatformIO env: **`supermini`**
- Serial: **115200** baud
- USB CDC on boot enabled in `platformio.ini` for the Super Mini

### Unit tests

The libraries in `lib/` have GoogleTest suites that run on the host (macOS or Linux), no board needed:

```bash
pio test -e native        # run the tests
scripts/coverage.sh       # run them and require 100% line coverage (needs gcovr: pipx install gcovr)
```

### Web-flashable release image

Single `.bin` for [esptool-js](https://espressif.github.io/esptool-js/) and similar tools (ESP32-C3, 4 MB, flash at **0x0**):

```bash
chmod +x scripts/merge-firmware.sh   # once
./scripts/merge-firmware.sh
```

Writes `release/plane-radar-merged.bin`. Skip rebuild if firmware is already built:

```bash
./scripts/merge-firmware.sh --no-build
```

Or via PlatformIO only (output: `.pio/build/supermini/firmware-merged.bin`):

```bash
pio run -e supermini
pio run -t merge -e supermini
```

Put the board in download mode (hold **BOOT**, tap **RESET**), then flash with Chrome/Edge over USB.

### CI and releases (GitHub Actions)

| Workflow | When | Output |
|----------|------|--------|
| [Build](.github/workflows/build.yml) | Push / PR to `main` | Host unit tests with coverage gate, CA check script tests; artifact `plane-radar-supermini` (merged + split `.bin` files, ~90 days) |
| [Release](.github/workflows/release.yml) | Git tag `v*` (e.g. `v1.0.0`) | GitHub Release asset `plane-radar-v1.0.0.bin` + `.sha256` |

To ship a version users can download:

```bash
git tag v1.0.0
git push origin v1.0.0
```

The release workflow builds firmware in CI and attaches the merged image to the release. Download from **Releases** on GitHub, then flash at **0x0** (ESP32-C3, 4 MB).

## Dependencies

- [LovyanGFX](https://github.com/lovyan03/LovyanGFX)
- [WiFiManager](https://github.com/tzapu/WiFiManager)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)

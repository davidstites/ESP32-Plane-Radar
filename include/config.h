#pragma once

#include <cstdint>

#include <driver/gpio.h>

#ifndef PLANE_RADAR_BUILD
#define PLANE_RADAR_BUILD 1  // release builds override this with -DPLANE_RADAR_BUILD=N
#endif

namespace config {

/** Monotonic build number compared against the signed manifest's `build`. */
constexpr uint32_t kFirmwareBuild = PLANE_RADAR_BUILD;

// --- Wi-Fi portal ---
constexpr char kPortalApName[] = "PlaneRadar-Setup";
constexpr char kPortalIp[] = "192.168.4.1";
/** mDNS host (no ".local" suffix); browser: http://plane-radar.local */
constexpr char kPortalHostname[] = "plane-radar";
constexpr char kPortalHostUrl[] = "plane-radar.local";

// --- Hosted location setup page (QR code and /location hand-off) ---
constexpr char kSetupPageUrl[] = "https://metarly.app/setup";
constexpr char kSetupPageShortUrl[] = "metarly.app/setup";

/** Per-attempt STA connect wait (ms); retried kWifiConnectAttempts times. */
constexpr unsigned long kWifiConnectAttemptMs = 15000;
constexpr uint8_t kWifiConnectAttempts = 3;
constexpr unsigned long kWifiPortalTimeoutSec = 0;  // 0 = no timeout while configuring
constexpr unsigned long kWifiConnectingFrameMs = 50;
/** Wait after disconnect before reconnecting (avoids portal on brief drops). */
constexpr unsigned long kWifiDownGraceMs = 4000;
/** Minimum interval between background reconnect tries. */
constexpr unsigned long kWifiReconnectIntervalMs = 15000;

// --- BOOT button (ESP32-C3 Super Mini, active LOW) ---
constexpr gpio_num_t kBootPin = GPIO_NUM_9;
constexpr unsigned long kBootResetHoldMs = 3000UL;
/** Ignore BOOT taps shorter than this (debounce). */
constexpr unsigned long kBootTapMinMs = 40UL;

// --- Display: GC9A01 1.28" round 240×240 (SPI) ---
constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_0;
constexpr gpio_num_t kDisplayPinCs = GPIO_NUM_1;
constexpr gpio_num_t kDisplayPinDc = GPIO_NUM_10;
constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_3;  // display SDA
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_4;  // display SCL

constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 240;

constexpr uint32_t kDisplaySpiWriteHz = 40000000;
// GC9A01 modules often need invert + BGR for correct black/green output
constexpr bool kDisplayInvert = true;
constexpr bool kDisplayRgbOrder = true;

// --- Radar center defaults (overridden via WiFi setup portal) ---
constexpr double kDefaultRadarLat = 52.3676;
constexpr double kDefaultRadarLon = 4.9041;

/** Poll adsb.fi (API public limit: 1 req/s). */
constexpr unsigned long kAdsbFetchIntervalMs = 5000;
/** Gap between successful weather-feed refreshes (METAR flight categories, radar). */
constexpr unsigned long kFeedRefreshIntervalMs = 15UL * 60UL * 1000UL;
/** Gap before trying the weather feeds again after a failed refresh. */
constexpr unsigned long kFeedRetryMs = 20UL * 1000UL;
// Firmware update checks: the first shortly after boot, then every 6 h plus a random extra
// of up to 6 h so a fleet of boards does not hit the server together. A failed check retries
// in 30 min.
constexpr unsigned long kOtaFirstCheckMs = 5UL * 60UL * 1000UL;
constexpr unsigned long kOtaIntervalMs = 6UL * 60UL * 60UL * 1000UL;
constexpr unsigned long kOtaJitterMaxMs = 6UL * 60UL * 60UL * 1000UL;
constexpr unsigned long kOtaRetryMs = 30UL * 60UL * 1000UL;
/** A freshly installed build that is not confirmed this long after boot restarts into the old one. */
constexpr unsigned long kOtaConfirmDeadlineMs = 10UL * 60UL * 1000UL;
/** Update server and the directory on it holding manifest.json, images and rc/ manifests. */
constexpr char kOtaHost[] = "metarly.app";
constexpr char kOtaBasePath[] = "/planeradar";
/** A range or location change must stay unchanged this long before the feeds refetch. */
constexpr unsigned long kViewSettleMs = 3UL * 1000UL;
/** Redraw cadence; aircraft are dead-reckoned along their track/speed so motion
 *  is smooth. ~4 Hz = 250 ms (panel scanout caps at ~24 Hz; the full-frame
 *  recompose+present is the practical limit ~10-15 Hz). */
constexpr unsigned long kRadarRedrawIntervalMs = 250;
/** false = hide aircraft with alt_baro "ground"; true = show them too. */
constexpr bool kAdsbShowGroundAircraft = false;

// --- UI colors (RGB565) — status screens ---
constexpr uint16_t kColorBlack = 0x0000;
constexpr uint16_t kColorYellow = 0xFFE0;
constexpr uint16_t kTextOnYellow = kColorBlack;
constexpr uint16_t kTextOnBlack = 0xFFFF;

}  // namespace config

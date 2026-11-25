/**
 * Plane Radar — WiFi setup, then radar UI on the round GC9A01 display.
 */

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "hardware/boot_button.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/crash_log.h"
#include "services/metar_client.h"
#include "services/net_worker.h"
#include "services/ota_service.h"
#include "services/precip_client.h"
#include "services/radar_location.h"
#include "services/setup_session.h"
#include "services/wifi_setup.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

namespace {

bool g_radar_visible = false;
bool g_setup_screen_visible = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_redraw_ms = 0;

void showSetupScreen() {
  char url[160];
  if (!services::setup::url(url, sizeof(url))) {
    return;
  }
  statusScreenSetupQr(url, WiFi.localIP().toString().c_str(), services::setup::token());
  g_setup_screen_visible = true;
}

// Radar once a location is saved; until then the setup QR screen.
void showRadarIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_radar_visible = false;
    g_setup_screen_visible = false;
    return;
  }
  if (!services::location::hasSaved()) {
    g_radar_visible = false;
    if (!g_setup_screen_visible) {
      showSetupScreen();
    }
    return;
  }
  g_setup_screen_visible = false;
  ui::radarDisplayDraw();
  g_radar_visible = true;
}

void onRangeTap() {
  ui::radar::rangeNext();
  char range_label[12];
  ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
  Serial.printf("Range: %s (outer ~%.0f km)\n", range_label,
                ui::radar::rangeCurrent().outer_km);

  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
  }
}

void handleBootButton() {
  bootButtonPollLongPress();
  if (bootButtonConsumeTap()) {
    onRangeTap();
  }
}

void onBootLongPress() {
  Serial.println("BOOT held — resetting WiFi");
  wifiResetCredentialsAndReboot();
}

}  // namespace

void setup() {
  services::crash_log::checkpoint("setup");
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Plane Radar");
  services::ota::init();

  bootButtonInit();
  bootButtonSetLongPressHandler(onBootLongPress);
  displayInit();
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }
  services::location::init();
  ui::radar::rangeInit();
  services::adsb::init();
  services::metar::init();
  services::precip::init();

  if (wifiSetupConnect()) {
    showRadarIfConnected();
  }

  // Start the background network task (core 0, away from the render loop on core 1).
  services::net::start();
}

void loop() {
  services::ota::confirmTick();
  handleBootButton();
  wifiLoop();

  if (WiFi.status() != WL_CONNECTED) {
    if (g_radar_visible) {
      Serial.println("WiFi lost — will reconnect");
      g_radar_visible = false;
    }
    g_setup_screen_visible = false;

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms = millis() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = millis();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        showRadarIfConnected();
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (!g_radar_visible) {
      showRadarIfConnected();
    } else if (millis() - g_last_redraw_ms >= config::kRadarRedrawIntervalMs) {
      // Redraw at 4 Hz with dead-reckoned positions; the ADS-B fetch runs on
      // its own task (adsbFetchTask).
      g_last_redraw_ms = millis();
      ui::radarDisplayRefreshAircraft();
    }
  }

  delay(10);
}

#pragma once

void statusScreenPortal();
void statusScreenConnectFailed();
void statusScreenWifiReset();

/** Black-on-white QR for `url`, with the short URL, device IP and setup code as text. */
void statusScreenSetupQr(const char* url, const char* ip, const char* token);

/** Saved-network connect animation (call Tick until connect finishes). */
void statusScreenConnectingBegin(const char* ssid);
void statusScreenConnectingTick();

#pragma once

#include <HTTPClient.h>
#include <WiFiClient.h>

#include <adsb_parse/body_reader.h>

#include <cstdint>

namespace services::http {

/** Register the response headers bodyReader() needs; call before http.begin()/GET(). */
void collectBodyHeaders(HTTPClient& http);

/**
 * Decoder for the body of the response http is holding (chunked, Content-Length or until
 * close). It never blocks on the socket, so it also works as an ArduinoJson reader.
 */
adsb_parse::BodyReader bodyReader(HTTPClient& http, WiFiClient& client, uint32_t timeout_ms);

}  // namespace services::http

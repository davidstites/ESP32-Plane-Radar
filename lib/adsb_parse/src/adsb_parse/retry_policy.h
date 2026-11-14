#pragma once

namespace adsb_parse {

// HTTPClient (Arduino core 2.0.14) codes, mirrored so this header stays Arduino-free.
constexpr int kHttpErrorConnectionRefused = -1;  // HTTPC_ERROR_CONNECTION_REFUSED
constexpr int kHttpErrorNotConnected = -4;       // HTTPC_ERROR_NOT_CONNECTED
// WiFiClientSecure::lastError() for a socket/TCP connect failure; mbedTLS errors are below it.
constexpr int kTlsErrorSocket = -1;

/**
 * Whether to try connecting again within the same fetch: only while TCP itself cannot
 * connect. A TLS handshake or certificate failure (tls_error < -1) waits for the next
 * fetch interval instead of repeating a full handshake every few milliseconds.
 */
inline bool shouldKeepTryingToConnect(int http_code, int tls_error) {
  const bool not_connected =
      http_code == kHttpErrorConnectionRefused || http_code == kHttpErrorNotConnected;
  return not_connected && tls_error >= kTlsErrorSocket;
}

/**
 * Whether a failed request should be retried once on a new connection: true only for
 * a transport error (http_code <= 0) on a kept-alive connection the server may have
 * closed while idle. HTTP status codes are never retried.
 */
inline bool shouldRetryOnFreshConnection(int http_code, bool connection_was_reused) {
  return connection_was_reused && http_code <= 0;
}

}  // namespace adsb_parse

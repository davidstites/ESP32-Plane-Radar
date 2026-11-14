#pragma once

#include <cstdint>

#include "adsb_parse/body_reader.h"

namespace adsb_parse {

/** Whole-request budget: connect attempts, a fresh connection's headers, and the body. */
constexpr uint32_t kRequestTimeoutMs = 10000;
/**
 * Header wait on a kept-alive connection. A server that closed it without a TLS
 * close_notify looks alive until this expires; the retry then uses a fresh connection.
 */
constexpr uint32_t kReusedHeaderTimeoutMs = 3000;

inline uint32_t headerTimeoutMs(bool connection_was_reused) {
  return connection_was_reused ? kReusedHeaderTimeoutMs : kRequestTimeoutMs;
}

/**
 * Whether a connection with no decrypted bytes waiting is closed, given the TLS layer's
 * view and a non-blocking MSG_PEEK recv() of one byte on its socket. mbedTLS reports a
 * peer FIN without close_notify as "0 bytes" rather than an error, so only the peek sees it.
 */
inline bool socketClosed(bool tls_connected, int peek_result, bool would_block) {
  if (!tls_connected || peek_result == 0) {
    return true;
  }
  return peek_result < 0 && !would_block;
}

enum class ErrorBodyAction { kKeep, kDrain, kClose };

/** What to do with the connection after a response other than 200 OK. */
inline ErrorBodyAction errorBodyAction(int http_code, BodyFraming framing) {
  if (http_code <= 0) {
    return ErrorBodyAction::kClose;
  }
  if (http_code < 200 || http_code == 204 || http_code == 304) {
    return ErrorBodyAction::kKeep;
  }
  return framing == BodyFraming::kUntilClose ? ErrorBodyAction::kClose : ErrorBodyAction::kDrain;
}

}  // namespace adsb_parse

#pragma once

namespace adsb_parse {

/**
 * Whether a failed request should be retried once on a new connection: true only for
 * a transport error (http_code <= 0) on a kept-alive connection the server may have
 * closed while idle. HTTP status codes are never retried.
 */
inline bool shouldRetryOnFreshConnection(int http_code, bool connection_was_reused) {
  return connection_was_reused && http_code <= 0;
}

}  // namespace adsb_parse

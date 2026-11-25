#pragma once

#include <cstddef>
#include <cstdint>

#if defined(ARDUINO)
#include "mbedtls/sha256.h"
#endif

namespace ota_core {

/**
 * Streaming SHA-256. The host build uses the portable implementation below; on the
 * firmware (ARDUINO defined) it wraps mbedtls, which uses the chip's hardware engine.
 */
class Sha256 {
 public:
  /** Begin a new hash, discarding any state from a previous one. */
  void start();
  void update(const uint8_t* data, size_t len);
  /** Write the 32-byte digest. Call start() before hashing again. */
  void finish(uint8_t out[32]);

 private:
#if defined(ARDUINO)
  mbedtls_sha256_context ctx_;
#else
  void compress(const uint8_t block[64]);

  uint32_t state_[8];
  uint8_t buf_[64];
  uint64_t total_bytes_;
  size_t used_;
#endif
};

}  // namespace ota_core

#pragma once

#include <cstddef>
#include <cstdint>

#include "ota_core/types.h"

namespace ota_core {

constexpr size_t kMaxManifestBytes = 1024;
constexpr size_t kMaxCandidates = 8;
constexpr size_t kPathMax = 96;

/** A parsed manifest. Nothing in it is trusted until verifyManifest accepts fields(). */
struct Manifest {
  U32Field build;
  U32Field minBuild;
  char sha256[65];
  char channel[8];
  char signature[89];
  char firmware[kPathMax + 1];
  uint8_t rollout;  // 0-100, unsigned and advisory; 100 when absent
  char candidates[kMaxCandidates][33];
  size_t candidateCount;

  /** View for verifyManifest. The pointers refer into this object. */
  ManifestFields fields() const {
    return ManifestFields{build, minBuild, sha256, channel, signature};
  }
};

enum class ParseError : uint8_t { kNone, kTooLarge, kNotJson, kMissingField, kBadField };

/**
 * Parse a manifest body. `build` and `min_build` that are present but not 32-bit unsigned
 * integers parse successfully with valid=false so the verifier can name them. Strings that
 * do not fit are kBadField, never truncated. `out` is reset first, even on failure.
 */
ParseError parseManifest(const char* body, size_t len, Manifest& out);

}  // namespace ota_core

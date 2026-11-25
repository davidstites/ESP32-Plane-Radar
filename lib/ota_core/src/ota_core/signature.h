#pragma once

#include <cstddef>
#include <cstdint>

#include "ota_core/types.h"

namespace ota_core {

/** Why a manifest was refused. Order is the order verifyManifest checks them in. */
enum class SigError : uint8_t {
  kNone,
  kMissingSignature,
  kBadSignatureEncoding,
  kUnknownChannel,
  kChannelNotAccepted,
  kBadHash,
  kBadBuild,
  kBadMinBuild,
  kMinBuildAboveBuild,
  kBadDeviceId,
  kBadSignature,
};

/** snake_case name for logs and the status page; "unknown" for out-of-range values. */
const char* sigErrorName(SigError e);

/** Strict base64 of exactly 64 bytes: 88 chars, "==" padding, canonical, standard alphabet. */
bool decodeSignatureB64(const char* b64, uint8_t out[kSigBytes]);

/** device_id = SHA-256("PLANERADAR-DEVID-v1\0" || mac). */
void computeDeviceId(const uint8_t mac6[6], uint8_t out[kDeviceIdBytes]);

/** First 16 bytes of the id as 32 lowercase hex characters plus NUL. */
void deviceHashHex(const uint8_t id[kDeviceIdBytes], char out[33]);

/** Assemble the 91-byte signed payload. Prod signs zeros in place of the device id. */
size_t buildPayload(const uint8_t sha[kShaBytes], uint32_t build, uint32_t minBuild, Channel channel,
                    const uint8_t deviceId[kDeviceIdBytes], uint8_t out[kPayloadBytes]);

/**
 * Check a manifest's signed fields. `out` is written only when this returns kNone.
 * `ownDeviceId` is this board's id; an rc signature only verifies for the board it names.
 */
SigError verifyManifest(const ManifestFields& f, uint8_t acceptedChannelMask,
                        const uint8_t ownDeviceId[kDeviceIdBytes], const KeyList& keys,
                        Ed25519VerifyFn verify, VerifiedOffer& out);

}  // namespace ota_core

#pragma once

#include <cstddef>
#include <cstdint>

#include "ota_core/types.h"

namespace ota_core {
namespace keys {

/**
 * Trusted release keys: primary, secondary, emergency. A slot of all zeros is disabled and
 * never verifies anything. Every slot ships zeroed until a release key is generated, so a
 * stock build accepts no update at all (fail closed).
 */
inline const uint8_t* const* slots() {
  static const uint8_t kPrimary[kKeyBytes] = {0};
  static const uint8_t kSecondary[kKeyBytes] = {0};
  static const uint8_t kEmergency[kKeyBytes] = {0};
  static const uint8_t* const kSlots[] = {kPrimary, kSecondary, kEmergency};
  return kSlots;
}

inline KeyList keyList() { return KeyList{slots(), 3}; }

inline bool isDisabled(const uint8_t key[kKeyBytes]) {
  uint8_t acc = 0;
  for (size_t i = 0; i < kKeyBytes; ++i) acc |= key[i];
  return acc == 0;
}

}  // namespace keys
}  // namespace ota_core

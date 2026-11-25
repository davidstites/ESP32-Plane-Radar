#pragma once

#include <cstddef>
#include <cstdint>

#include "ota_core/types.h"

namespace ota_core {
namespace keys {

/**
 * Trusted release keys: primary, secondary, emergency. A slot of all zeros is disabled and
 * never verifies anything. Primary and secondary hold the release keys; emergency is unused
 * and stays zeroed. A build with every slot zeroed would accept no update (fail closed).
 */
inline const uint8_t* const* slots() {
  static const uint8_t kPrimary[kKeyBytes] = {
      0x47, 0x15, 0x6c, 0xe5, 0xca, 0xaf, 0x0d, 0xc2, 0x16, 0x4d, 0x38, 0x92, 0x6c, 0xe9, 0x4a, 0x71,
      0xd0, 0x21, 0xbd, 0x1a, 0x23, 0x89, 0x7a, 0x32, 0xb1, 0x97, 0xaf, 0x95, 0xca, 0x5b, 0x73, 0x4d};
  static const uint8_t kSecondary[kKeyBytes] = {
      0x7e, 0x5d, 0xf6, 0x1d, 0x12, 0x79, 0x00, 0x36, 0x61, 0xf0, 0x80, 0x6f, 0x4e, 0x56, 0x78, 0x59,
      0x99, 0xd4, 0x99, 0x2a, 0x80, 0x17, 0xed, 0x91, 0xef, 0x57, 0xaa, 0x31, 0x53, 0x19, 0x34, 0x50};
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

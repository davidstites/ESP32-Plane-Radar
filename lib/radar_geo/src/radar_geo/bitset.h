#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace radar_geo {

/** Fixed-size bit set: one bit per index instead of one byte per bool. */
template <size_t N>
class Bitset {
 public:
  Bitset() { clear(); }

  bool test(size_t i) const { return (bytes_[i >> 3] >> (i & 7)) & 1u; }

  void set(size_t i) { bytes_[i >> 3] |= static_cast<uint8_t>(1u << (i & 7)); }

  void clear() { memset(bytes_, 0, sizeof(bytes_)); }

 private:
  uint8_t bytes_[(N + 7) / 8];
};

}  // namespace radar_geo

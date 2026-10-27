#pragma once

// The CRC-32 ZIP files use (polynomial 0xEDB88320): the data export's ZIP (data_transfer.cpp) and
// the portable launcher's update (portable.cpp).

#include <array>
#include <cstddef>
#include <cstdint>

namespace coreoflight {

inline uint32_t Crc32(const char* data, size_t size) {
  static const std::array<uint32_t, 256> table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < size; ++i) crc = table[(crc ^ uint8_t(data[i])) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

}  // namespace coreoflight

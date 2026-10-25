#pragma once
// CRC-32 (IEEE) for the small on-device state files. Pure C++.
#include <cstddef>
#include <cstdint>

namespace kk {

inline uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

inline uint32_t crc32(const uint8_t* data, size_t n) {
    return crc32Update(0xFFFFFFFFu, data, n) ^ 0xFFFFFFFFu;
}

}  // namespace kk

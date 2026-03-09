#pragma once
// crc32.h — Software CRC32 (IEEE 802.3 / zlib reflected polynomial 0xEDB88320).
//
// Header-only; no lookup table, suitable for infrequent flash record operations.

#include <cstddef>
#include <cstdint>

inline uint32_t crc32_bytes(const void* data, size_t len)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b) {
            // Reflected Galois LFSR: shift right and XOR with polynomial if bit 0 was set.
            crc = (crc >> 1u) ^ (0xEDB88320u & static_cast<uint32_t>(-(crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

// Streaming variant — call with init=0xFFFFFFFF, finalize with ^0xFFFFFFFF.
inline uint32_t crc32_update(uint32_t crc, const void* data, size_t len)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1u) ^ (0xEDB88320u & static_cast<uint32_t>(-(crc & 1u)));
        }
    }
    return crc;
}

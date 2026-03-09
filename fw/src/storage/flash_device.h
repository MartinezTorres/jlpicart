#pragma once
// flash_device.h — Bounded flash read/write/erase abstraction.
//
// On firmware: wraps RP2350 XIP (reads) and flash_range_program/erase (writes).
// On host tests: backed by a RAM buffer; supports power-loss injection.
//
// Thread safety: NOT thread-safe.  On firmware, the caller must ensure Core 1
// is not accessing flash and interrupts that touch flash are disabled before
// calling write() or erase().

#include "diag/diag.h"
#include "storage/flash_layout.h"
#include <cstddef>
#include <cstdint>

#ifdef JLPICART_HOST_TEST
#include <vector>
#endif

class FlashDevice {
public:
#ifdef JLPICART_HOST_TEST
    // Host-test constructor: backs the device with a zero-initialised RAM buffer
    // of `capacity` bytes, with all bytes set to 0xFF (erased state).
    explicit FlashDevice(size_t capacity);

    // Simulate a power-loss mid-write: the next `bytes` bytes of write() calls
    // will succeed; any bytes after that are silently discarded (power gone).
    // Pass SIZE_MAX to clear any pending injection.
    void inject_power_loss_after(size_t bytes);

    // Direct access for host-test inspection.
    const uint8_t* raw() const;
    size_t         capacity() const;

#else
    FlashDevice() = default;

    // Returns the singleton wrapping the hardware RP2350 flash.
    static FlashDevice& hardware();
#endif

    // Read `len` bytes from flash at `offset` into `dst`.
    DiagStatus read(uint32_t offset, uint8_t* dst, size_t len) const;

    // Write `len` bytes from `src` to flash at `offset`.
    // On firmware: offset must be within a programmed sector; page-alignment
    // is handled internally (writes are padded to FLASH_PAGE_SIZE multiples).
    // The region must have been erased before writing.
    DiagStatus write(uint32_t offset, const uint8_t* src, size_t len);

    // Erase `sector_count` sectors starting at `sector_offset`.
    // `sector_offset` must be sector-aligned; `sector_count >= 1`.
    DiagStatus erase(uint32_t sector_offset, size_t sector_count);

    // True if [offset, offset+len) is within [0, total_capacity).
    bool in_bounds(uint32_t offset, size_t len) const;

private:
#ifdef JLPICART_HOST_TEST
    std::vector<uint8_t> buf_;
    size_t               power_loss_remaining_ = static_cast<size_t>(-1);
#endif
};

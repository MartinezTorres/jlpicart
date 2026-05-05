// flash_device.cc — FlashDevice implementation (host sim or RP2350 hardware).

#include "filesystem/flash_device.h"
#include <cstring>
#include <algorithm>

// ---------------------------------------------------------------------------
// Host-test implementation (RAM-backed)
// ---------------------------------------------------------------------------

#ifdef JLPICART_HOST_TEST

FlashDevice::FlashDevice(size_t capacity)
    : buf_(capacity, 0xFFu)
{
}

void FlashDevice::inject_power_loss_after(size_t bytes)
{
    power_loss_remaining_ = bytes;
}

const uint8_t* FlashDevice::raw() const { return buf_.data(); }
size_t         FlashDevice::capacity() const { return buf_.size(); }

bool FlashDevice::in_bounds(uint32_t offset, size_t len) const
{
    return len == 0 || (static_cast<size_t>(offset) + len <= buf_.size());
}

DiagStatus FlashDevice::read(uint32_t offset, uint8_t* dst, size_t len) const
{
    if (!in_bounds(offset, len)) {
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    }
    memcpy(dst, buf_.data() + offset, len);
    return DiagStatus::success();
}

DiagStatus FlashDevice::write(uint32_t offset, const uint8_t* src, size_t len)
{
    if (!in_bounds(offset, len)) {
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    }
    // Honour power-loss injection: write at most power_loss_remaining_ bytes.
    size_t actual = std::min(len, power_loss_remaining_);
    memcpy(buf_.data() + offset, src, actual);
    power_loss_remaining_ -= actual;
    if (actual < len) {
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    }
    return DiagStatus::success();
}

DiagStatus FlashDevice::erase(uint32_t sector_offset, size_t sector_count)
{
    if ((sector_offset % FLASH_SECTOR_SIZE) != 0) {
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    }
    size_t erase_len = sector_count * FLASH_SECTOR_SIZE;
    if (!in_bounds(sector_offset, erase_len)) {
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    }
    memset(buf_.data() + sector_offset, 0xFFu, erase_len);
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// Firmware (RP2350) implementation
// ---------------------------------------------------------------------------

#else  // !JLPICART_HOST_TEST

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"

// The RP2350 XIP window maps flash at XIP_BASE; reads are just pointer deref.
// Writes and erases go through the ROM flash functions and MUST be called
// with interrupts disabled and Core 1 not executing from flash.

bool FlashDevice::in_bounds(uint32_t offset, size_t len) const
{
    return len == 0 || (static_cast<uint64_t>(offset) + len <= FLASH_SIZE_BYTES);
}

DiagStatus FlashDevice::read(uint32_t offset, uint8_t* dst, size_t len) const
{
    if (!in_bounds(offset, len)) {
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    }
    const uint8_t* flash_ptr =
        reinterpret_cast<const uint8_t*>(XIP_BASE + offset);
    memcpy(dst, flash_ptr, len);
    return DiagStatus::success();
}

DiagStatus FlashDevice::write(uint32_t offset, const uint8_t* src, size_t len)
{
    if (len == 0) return DiagStatus::success();
    if (!in_bounds(offset, len)) {
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    }
    // flash_range_program requires a page-aligned offset and a length that is
    // a multiple of FLASH_PAGE_SIZE.  Log-structured appends are variable-size
    // so writes commonly start mid-page.  For each page the write touches:
    //   1. Read current page content from XIP into a staging buffer.
    //   2. Overlay the new bytes within that page.
    //   3. Program the merged page.
    // NOR flash only allows 1→0 transitions per program cycle, so re-reading
    // already-programmed bits and writing them back unchanged is safe.
    uint32_t end  = offset + static_cast<uint32_t>(len);
    uint32_t page = (offset / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE;
    while (page < end) {
        uint8_t staging[FLASH_PAGE_SIZE];
        const uint8_t* xip = reinterpret_cast<const uint8_t*>(XIP_BASE + page);
        memcpy(staging, xip, FLASH_PAGE_SIZE);

        uint32_t copy_start = (offset > page) ? offset : page;
        uint32_t copy_end   = (end < page + FLASH_PAGE_SIZE) ? end
                                                              : page + FLASH_PAGE_SIZE;
        memcpy(staging + (copy_start - page),
               src    + (copy_start - offset),
               copy_end - copy_start);

        flash_range_program(page, staging, FLASH_PAGE_SIZE);
        page += FLASH_PAGE_SIZE;
    }
    // Invalidate the XIP cache so subsequent reads through the XIP window
    // return the newly programmed data rather than stale cached contents.
    flash_flush_cache();
    return DiagStatus::success();
}

DiagStatus FlashDevice::erase(uint32_t sector_offset, size_t sector_count)
{
    if ((sector_offset % FLASH_SECTOR_SIZE) != 0) {
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    }
    size_t erase_len = sector_count * FLASH_SECTOR_SIZE;
    if (!in_bounds(sector_offset, erase_len)) {
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    }
    flash_range_erase(sector_offset, erase_len);
    return DiagStatus::success();
}

static FlashDevice s_hardware_device;
FlashDevice& FlashDevice::hardware() { return s_hardware_device; }

#endif  // JLPICART_HOST_TEST

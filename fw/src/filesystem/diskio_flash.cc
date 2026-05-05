// diskio_flash.cc — FatFs diskio backend for the internal QSPI flash (drive 1).
//
// Reads go directly through the XIP window (zero-copy memcpy).
// Writes use read-modify-write: read the 4 KB flash sector into a stack buffer,
// patch the affected 512-byte FatFs sectors, erase, then write back.
//
// Thread safety: NOT safe for concurrent access.  All writes must happen while
// Core 1 is not accessing flash (boot time, menu operations, or USB device mode).
//
// Hardware-only: not compiled for host-test builds.

#ifndef JLPICART_HOST_TEST

#include "filesystem/diskio_flash.h"
#include "filesystem/flash_device.h"
#include "filesystem/flash_layout.h"
#include <hardware/regs/addressmap.h>  // XIP_BASE
#include <cstring>

// FAT volume occupies drive 1.
static constexpr BYTE   FLASH_DRIVE     = 1u;
// 512-byte logical sectors to match the existing FatFs configuration.
static constexpr DWORD  SECTOR_SIZE     = 512u;
static constexpr DWORD  TOTAL_SECTORS   =
    static_cast<DWORD>(FLASH_FAT_SIZE / SECTOR_SIZE);
// Flash erase granularity: 8 FatFs sectors per 4 KB erase block.
static constexpr DWORD  SECTORS_PER_BLOCK =
    static_cast<DWORD>(FLASH_SECTOR_SIZE / SECTOR_SIZE);

DSTATUS flash_disk_status(BYTE pdrv) {
    return (pdrv == FLASH_DRIVE) ? 0 : STA_NOINIT;
}

DSTATUS flash_disk_initialize(BYTE pdrv) {
    return (pdrv == FLASH_DRIVE) ? 0 : STA_NOINIT;
}

DRESULT flash_disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv != FLASH_DRIVE) return RES_PARERR;
    if (sector + count > TOTAL_SECTORS) return RES_PARERR;

    const uint8_t* src = reinterpret_cast<const uint8_t*>(
        XIP_BASE + FLASH_FAT_OFS + sector * SECTOR_SIZE);
    memcpy(buff, src, count * SECTOR_SIZE);
    return RES_OK;
}

DRESULT flash_disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv != FLASH_DRIVE) return RES_PARERR;
    if (sector + count > TOTAL_SECTORS) return RES_PARERR;

    FlashDevice& flash = FlashDevice::hardware();
    // Stack buffer for the read-modify-write of each 4 KB erase block.
    uint8_t block_buf[FLASH_SECTOR_SIZE];

    LBA_t s = sector;
    while (s < sector + static_cast<LBA_t>(count)) {
        // Which 4 KB erase block does sector s fall in?
        uint32_t block_flash_ofs = FLASH_FAT_OFS
            + (s / SECTORS_PER_BLOCK) * FLASH_SECTOR_SIZE;

        // Read the entire 4 KB block from XIP.
        memcpy(block_buf,
               reinterpret_cast<const uint8_t*>(XIP_BASE + block_flash_ofs),
               FLASH_SECTOR_SIZE);

        // Patch all FatFs sectors in this block that fall within the write range.
        uint32_t first_sector_in_block = (s / SECTORS_PER_BLOCK) * SECTORS_PER_BLOCK;
        uint32_t last_sector_in_block  = first_sector_in_block + SECTORS_PER_BLOCK;
        for (LBA_t i = s; i < sector + count && i < last_sector_in_block; ++i) {
            uint32_t offset_in_block =
                static_cast<uint32_t>(i - first_sector_in_block) * SECTOR_SIZE;
            const uint8_t* src = buff + (i - sector) * SECTOR_SIZE;
            memcpy(block_buf + offset_in_block, src, SECTOR_SIZE);
        }

        // Erase and write back the block.
        if (!flash.erase(block_flash_ofs, 1).ok()) return RES_ERROR;
        if (!flash.write(block_flash_ofs, block_buf, FLASH_SECTOR_SIZE).ok())
            return RES_ERROR;

        s = last_sector_in_block;
    }
    return RES_OK;
}

DRESULT flash_disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    if (pdrv != FLASH_DRIVE) return RES_PARERR;
    switch (cmd) {
        case CTRL_SYNC:
            return RES_OK;
        case GET_SECTOR_COUNT:
            *static_cast<LBA_t*>(buff) = static_cast<LBA_t>(TOTAL_SECTORS);
            return RES_OK;
        case GET_SECTOR_SIZE:
            *static_cast<WORD*>(buff) = static_cast<WORD>(SECTOR_SIZE);
            return RES_OK;
        case GET_BLOCK_SIZE:
            *static_cast<DWORD*>(buff) = static_cast<DWORD>(SECTORS_PER_BLOCK);
            return RES_OK;
        default:
            return RES_PARERR;
    }
}

#endif  // !JLPICART_HOST_TEST

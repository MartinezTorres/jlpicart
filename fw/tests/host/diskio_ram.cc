// diskio_ram.cc — RAM-backed FatFs diskio driver for host-test builds.
//
// Provides the disk_* functions required by ff.c for pdrv=1 (the internal
// flash drive used by all FAT-backed stores).  Uses a heap-allocated buffer
// so FatFs can format, mount, and write files in host tests without hardware.
//
// Drive geometry: 2048 × 512-byte sectors = 1 MB.
// Call ram_disk_reset() between tests to start with a blank (unformatted) disk.

#include "ff.h"
#include "diskio.h"
#include <cstdlib>
#include <cstring>
#include <cassert>

static constexpr DWORD kSectorSize  = 512u;
static constexpr DWORD kSectorCount = 2048u;   // 1 MB

static uint8_t* g_disk        = nullptr;
static bool     g_initialized = false;

void ram_disk_reset()
{
    if (!g_disk) {
        g_disk = static_cast<uint8_t*>(malloc(kSectorCount * kSectorSize));
        assert(g_disk);
    }
    memset(g_disk, 0xFF, kSectorCount * kSectorSize);
    g_initialized = false;
}

extern "C" {

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 1u) return STA_NOINIT;
    if (!g_disk) ram_disk_reset();
    g_initialized = true;
    return 0;
}

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != 1u) return STA_NOINIT;
    return g_initialized ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count)
{
    if (pdrv != 1u || !g_disk) return RES_NOTRDY;
    if (static_cast<DWORD>(sector) + count > kSectorCount) return RES_PARERR;
    memcpy(buff, g_disk + sector * kSectorSize, count * kSectorSize);
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count)
{
    if (pdrv != 1u || !g_disk) return RES_NOTRDY;
    if (static_cast<DWORD>(sector) + count > kSectorCount) return RES_PARERR;
    memcpy(g_disk + sector * kSectorSize, buff, count * kSectorSize);
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff)
{
    if (pdrv != 1u) return RES_NOTRDY;
    switch (cmd) {
    case CTRL_SYNC:        return RES_OK;
    case GET_SECTOR_COUNT: *static_cast<DWORD*>(buff) = kSectorCount; return RES_OK;
    case GET_SECTOR_SIZE:  *static_cast<WORD*>(buff)  = static_cast<WORD>(kSectorSize); return RES_OK;
    case GET_BLOCK_SIZE:   *static_cast<DWORD*>(buff) = 1u; return RES_OK;
    default:               return RES_PARERR;
    }
}

} // extern "C"

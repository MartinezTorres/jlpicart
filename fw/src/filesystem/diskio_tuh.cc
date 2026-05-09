// diskio_tuh.cc — FatFs diskio backend wired to tinyusb MSC host (drive 0).
//
// Only compiled in firmware builds (not in JLPICART_HOST_TEST builds).
// The public disk_* symbols live in src/storage/diskio.cc (dispatcher).

#ifndef JLPICART_HOST_TEST

#include "filesystem/diskio_backend.h"
#include "ff.h"
#include "tusb.h"
#include "usb/usb_host.h"

static uint8_t disk_dev_addr() {
    for (uint8_t addr = 1; addr <= 4; ++addr) {
        if (tuh_msc_get_block_count(addr, 0) > 0) return addr;
    }
    return 0;
}

static volatile bool g_io_done = false;
static volatile bool g_io_ok   = false;

static bool read_complete_cb(uint8_t dev_addr,
                              tuh_msc_complete_data_t const* cb_data) {
    (void)dev_addr;
    g_io_ok   = (cb_data->csw->status == 0);
    g_io_done = true;
    return true;
}

DSTATUS tuh_disk_status(BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;
    return (disk_dev_addr() != 0) ? 0 : STA_NOINIT;
}

DSTATUS tuh_disk_initialize(BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;
    return (disk_dev_addr() != 0) ? 0 : STA_NOINIT;
}

DRESULT tuh_disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv != 0) return RES_PARERR;
    uint8_t addr = disk_dev_addr();
    if (addr == 0) return RES_NOTRDY;

    g_io_done = false;
    g_io_ok   = false;
    if (!tuh_msc_read10(addr, 0, buff,
                        static_cast<uint32_t>(sector),
                        static_cast<uint16_t>(count),
                        read_complete_cb, 0)) {
        return RES_ERROR;
    }
    while (!g_io_done) tuh_task();
    return g_io_ok ? RES_OK : RES_ERROR;
}

DRESULT tuh_disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
    (void)pdrv; (void)buff; (void)sector; (void)count;
    return RES_WRPRT;  // USB drives are read-only: we install from USB, never write back
}

DRESULT tuh_disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    if (pdrv != 0) return RES_PARERR;
    uint8_t addr = disk_dev_addr();
    if (addr == 0) return RES_NOTRDY;

    switch (cmd) {
        case CTRL_SYNC:
            return RES_OK;
        case GET_SECTOR_COUNT: {
            *static_cast<LBA_t*>(buff) =
                static_cast<LBA_t>(tuh_msc_get_block_count(addr, 0));
            return RES_OK;
        }
        case GET_SECTOR_SIZE: {
            *static_cast<WORD*>(buff) =
                static_cast<WORD>(tuh_msc_get_block_size(addr, 0));
            return RES_OK;
        }
        case GET_BLOCK_SIZE:
            *static_cast<DWORD*>(buff) = 1;
            return RES_OK;
        default:
            return RES_PARERR;
    }
}

#endif  // !JLPICART_HOST_TEST

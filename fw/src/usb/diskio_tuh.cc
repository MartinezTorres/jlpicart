// diskio_tuh.cc — FatFs diskio backend wired to tinyusb MSC host.
//
// Implements the four FatFs low-level disk functions required by ff.c:
//   disk_status()     — return STA_NOINIT if not mounted
//   disk_initialize() — check mount state
//   disk_read()       — call tuh_msc_read10(), poll until complete
//   disk_write()      — return RES_WRPRT (read-only policy)
//   disk_ioctl()      — GET_SECTOR_COUNT / GET_SECTOR_SIZE / GET_BLOCK_SIZE
//
// Only compiled in firmware builds (not in JLPICART_HOST_TEST builds).

#ifndef JLPICART_HOST_TEST

#include "ff.h"
#include "diskio.h"
#include "tusb.h"
#include "usb/usb_host.h"

// Device address of the mounted MSC device (0 = none).
// Set by UsbHost::on_mount via tuh_msc_mount_cb.
// We read it through the stable accessor below.
static uint8_t disk_dev_addr() {
    // The mounted device address is tracked in UsbHost static state.
    // We reach it by calling tuh_msc_get_block_count with a probe — if it
    // returns non-zero the device is valid.  For address we use 1 (the
    // typical USB root-hub device address) and fall back to scan if needed.
    // NOTE: this is a firmware-internal heuristic; in production there is
    // only ever one MSC device.
    for (uint8_t addr = 1; addr <= 4; ++addr) {
        if (tuh_msc_get_block_count(addr, 0) > 0) return addr;
    }
    return 0;
}

// Completion flag + result for synchronous disk_read().
static volatile bool     g_io_done   = false;
static volatile bool     g_io_ok     = false;

static bool read_complete_cb(uint8_t dev_addr,
                               tuh_msc_complete_data_t const* cb_data) {
    (void)dev_addr;
    g_io_ok   = (cb_data->csw.status == 0);
    g_io_done = true;
    return true;
}

// ---------------------------------------------------------------------------
// FatFs disk interface
// ---------------------------------------------------------------------------

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;
    return UsbHost::on_mount, // trick: just check the static flag via accessor
           (disk_dev_addr() != 0) ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;
    return (disk_dev_addr() != 0) ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
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

    // Poll tuh_task() until the async read completes.
    while (!g_io_done) {
        tuh_task();
    }

    return g_io_ok ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
    (void)pdrv; (void)buff; (void)sector; (void)count;
    return RES_WRPRT;   // read-only: we install from USB but never write back
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    if (pdrv != 0) return RES_PARERR;

    uint8_t addr = disk_dev_addr();
    if (addr == 0) return RES_NOTRDY;

    switch (cmd) {
        case CTRL_SYNC:
            return RES_OK;

        case GET_SECTOR_COUNT: {
            LBA_t* out = static_cast<LBA_t*>(buff);
            *out = static_cast<LBA_t>(tuh_msc_get_block_count(addr, 0));
            return RES_OK;
        }

        case GET_SECTOR_SIZE: {
            WORD* out = static_cast<WORD*>(buff);
            *out = static_cast<WORD>(tuh_msc_get_block_size(addr, 0));
            return RES_OK;
        }

        case GET_BLOCK_SIZE: {
            DWORD* out = static_cast<DWORD*>(buff);
            *out = 1;   // 1 sector per erase block (irrelevant for read-only)
            return RES_OK;
        }

        default:
            return RES_PARERR;
    }
}

#endif // JLPICART_HOST_TEST

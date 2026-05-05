// diskio.cc — FatFs disk I/O dispatcher.
//
// Defines the global disk_* functions required by ff.c and routes them
// to the appropriate backend by physical drive number:
//
//   pdrv 0 — USB host MSC (tuh backend, src/usb/diskio_tuh.cc)
//   pdrv 1 — Internal QSPI flash FAT volume (src/storage/diskio_flash.cc)
//
// Hardware-only: not compiled for host-test builds.

#ifndef JLPICART_HOST_TEST

#include "ff.h"
#include "diskio.h"
#include "filesystem/diskio.h"
#include "filesystem/diskio.h"

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv == 0) return tuh_disk_status(pdrv);
    if (pdrv == 1) return flash_disk_status(pdrv);
    return STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv == 0) return tuh_disk_initialize(pdrv);
    if (pdrv == 1) return flash_disk_initialize(pdrv);
    return STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv == 0) return tuh_disk_read(pdrv, buff, sector, count);
    if (pdrv == 1) return flash_disk_read(pdrv, buff, sector, count);
    return RES_PARERR;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv == 0) return tuh_disk_write(pdrv, buff, sector, count);
    if (pdrv == 1) return flash_disk_write(pdrv, buff, sector, count);
    return RES_PARERR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    if (pdrv == 0) return tuh_disk_ioctl(pdrv, cmd, buff);
    if (pdrv == 1) return flash_disk_ioctl(pdrv, cmd, buff);
    return RES_PARERR;
}

#endif  // !JLPICART_HOST_TEST

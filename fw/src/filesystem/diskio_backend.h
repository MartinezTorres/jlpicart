#pragma once
// diskio_backend.h — FatFs disk backend declarations for internal flash and USB host.

#include "ff.h"
#include "diskio.h"

// Internal flash backend (drive 1: 14 MB FAT volume in QSPI flash).
DSTATUS flash_disk_status    (BYTE pdrv);
DSTATUS flash_disk_initialize(BYTE pdrv);
DRESULT flash_disk_read      (BYTE pdrv, BYTE* buff, LBA_t sector, UINT count);
DRESULT flash_disk_write     (BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count);
DRESULT flash_disk_ioctl     (BYTE pdrv, BYTE cmd, void* buff);

// USB host backend (drive 0: mounted USB MSC device).
DSTATUS tuh_disk_status    (BYTE pdrv);
DSTATUS tuh_disk_initialize(BYTE pdrv);
DRESULT tuh_disk_read      (BYTE pdrv, BYTE* buff, LBA_t sector, UINT count);
DRESULT tuh_disk_write     (BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count);
DRESULT tuh_disk_ioctl     (BYTE pdrv, BYTE cmd, void* buff);

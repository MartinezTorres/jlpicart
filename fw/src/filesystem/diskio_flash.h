#pragma once
// diskio_flash.h — FatFs internal flash disk backend (drive 1).
//
// Implements disk I/O over the 14 MB FAT volume in QSPI flash.
// Called by the top-level diskio dispatcher in src/storage/diskio.cc.

#include "diskio.h"

DSTATUS flash_disk_status    (BYTE pdrv);
DSTATUS flash_disk_initialize(BYTE pdrv);
DRESULT flash_disk_read      (BYTE pdrv, BYTE* buff, LBA_t sector, UINT count);
DRESULT flash_disk_write     (BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count);
DRESULT flash_disk_ioctl     (BYTE pdrv, BYTE cmd, void* buff);

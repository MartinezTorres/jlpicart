#pragma once
// diskio_tuh.h — FatFs USB host disk backend (drive 0).
//
// These functions implement the disk I/O for a mounted USB MSC device.
// They are called by the top-level diskio dispatcher in src/storage/diskio.cc.

#include "diskio.h"

DSTATUS tuh_disk_status    (BYTE pdrv);
DSTATUS tuh_disk_initialize(BYTE pdrv);
DRESULT tuh_disk_read      (BYTE pdrv, BYTE* buff, LBA_t sector, UINT count);
DRESULT tuh_disk_write     (BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count);
DRESULT tuh_disk_ioctl     (BYTE pdrv, BYTE cmd, void* buff);

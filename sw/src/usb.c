

#include "printf/printf.h"
#include "bsp/board_api.h"
#include "tusb.h"

void tuh_mount_cb(uint8_t dev_addr) {
  // application set-up
  printf("A device with address %d is mounted\r\n", dev_addr);
}

void tuh_umount_cb(uint8_t dev_addr) {
  // application tear-down
  printf("A device with address %d is unmounted \r\n", dev_addr);
}


#include <inttypes.h>

//--------------------------------------------------------------------+
// MACRO TYPEDEF CONSTANT ENUM DECLARATION
//--------------------------------------------------------------------+

uint8_t read_buffer[32][2048];
bool exit_usb;

bool read10_complete_cb(uint8_t dev_addr, tuh_msc_complete_data_t const * cb_data) {

  for (int i=0; i<8; i++) {
    printf("%02X ", read_buffer[0][i]);
  }
  printf("\n");

    for (int i=0; i<32; i++) {
        bool good = false;
        for (int j=0; j<2048; j++) {
            if (read_buffer[i][j]!=0xFF) {
                good = true;
            }
        }
        if (good) {
          printf("Block: %d OK\n", i);
        } else {
          printf("Block: %d NOT OK\n", i);
        }
    }

    exit_usb = true;
}


static scsi_inquiry_resp_t inquiry_resp;

bool inquiry_complete_cb(uint8_t dev_addr, tuh_msc_complete_data_t const * cb_data)
{
  msc_cbw_t const* cbw = cb_data->cbw;
  msc_csw_t const* csw = cb_data->csw;

  if (csw->status != 0)
  {
    printf("Inquiry failed\r\n");
    return false;
  }

  // Print out Vendor ID, Product ID and Rev
  printf("%.8s %.16s rev %.4s\r\n", inquiry_resp.vendor_id, inquiry_resp.product_id, inquiry_resp.product_rev);

  // Get capacity of device
  uint32_t const block_count = tuh_msc_get_block_count(dev_addr, cbw->lun);
  uint32_t const block_size = tuh_msc_get_block_size(dev_addr, cbw->lun);

  printf("Disk Size: %" PRIu32 " MB\r\n", block_count / ((1024*1024)/block_size));
  printf("Block Count = %" PRIu32 ", Block Size: %" PRIu32 "\r\n", block_count, block_size);

  //bool tuh_msc_read10(uint8_t dev_addr, uint8_t lun, void * buffer, uint32_t lba, uint16_t block_count, tuh_msc_complete_cb_t complete_cb, uintptr_t arg);

  for (int i=0; i<32; i++) {
        for (int j=0; j<2048; j++) {
            read_buffer[i][j]=0xFF;
        }
    }
  tuh_msc_read10(dev_addr, cbw->lun, &read_buffer[0][0], 0, 16, read10_complete_cb, 0 );

  return true;
}

//------------- IMPLEMENTATION -------------//
void tuh_msc_mount_cb(uint8_t dev_addr)
{
  printf("A MassStorage device is mounted\r\n");

  uint8_t const lun = 0;
  tuh_msc_inquiry(dev_addr, lun, &inquiry_resp, inquiry_complete_cb, 0);
}

void tuh_msc_umount_cb(uint8_t dev_addr)
{
  (void) dev_addr;
  printf("A MassStorage device is unmounted\r\n");
}


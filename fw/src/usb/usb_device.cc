// usb_device.cc — USB MSC device mode (internal flash, drive 1).
//
// Hardware-only: not compiled for host-test builds.

#ifndef JLPICART_HOST_TEST

#include "usb/usb_device.h"
#include "storage/diskio_flash.h"
#include "storage/flash_layout.h"
#include "diag/log.h"
#include "tusb.h"

// ---------------------------------------------------------------------------
// USB descriptors
// ---------------------------------------------------------------------------

// VID/PID: registered placeholder — must be updated before production.
static constexpr uint16_t USB_VID = 0x1209u;  // pid.codes community VID
static constexpr uint16_t USB_PID = 0x4A4Cu;  // "JL"

static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200u,
    .bDeviceClass       = 0x00u,
    .bDeviceSubClass    = 0x00u,
    .bDeviceProtocol    = 0x00u,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100u,
    .iManufacturer      = 0x01u,
    .iProduct           = 0x02u,
    .iSerialNumber      = 0x03u,
    .bNumConfigurations = 0x01u,
};

enum { ITF_NUM_MSC = 0, ITF_NUM_TOTAL };

static constexpr uint16_t CONFIG_TOTAL_LEN =
    TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN;

static constexpr uint8_t EPNUM_MSC_OUT = 0x01u;
static constexpr uint8_t EPNUM_MSC_IN  = 0x81u;

static const uint8_t desc_fs_config[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00u, 100u),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EPNUM_MSC_OUT, EPNUM_MSC_IN,
                       CFG_TUD_MSC_EP_BUFSIZE),
};

static const char* const string_desc_arr[] = {
    "\x09\x04",          // 0: language = English (0x0409)
    "JLPiCart",          // 1: Manufacturer
    "JLPiCart Storage",  // 2: Product
    "000001",            // 3: Serial number (fixed; no chip-ID read here)
};

// ---------------------------------------------------------------------------
// tinyusb device callbacks
// ---------------------------------------------------------------------------

extern "C" {

const uint8_t* tud_descriptor_device_cb(void) {
    return reinterpret_cast<const uint8_t*>(&desc_device);
}

const uint8_t* tud_descriptor_configuration_cb(uint8_t /*index*/) {
    return desc_fs_config;
}

static uint16_t desc_str_buf[32];

const uint16_t* tud_descriptor_string_cb(uint8_t index, uint16_t /*langid*/) {
    uint8_t chr_count;
    if (index == 0u) {
        memcpy(&desc_str_buf[1], string_desc_arr[0], 2u);
        chr_count = 1u;
    } else {
        if (index >= (sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))
            return nullptr;
        const char* str = string_desc_arr[index];
        chr_count = static_cast<uint8_t>(strlen(str));
        if (chr_count > 31u) chr_count = 31u;
        for (uint8_t i = 0u; i < chr_count; ++i)
            desc_str_buf[1u + i] = static_cast<uint16_t>(str[i]);
    }
    desc_str_buf[0] = static_cast<uint16_t>(
        (TUSB_DESC_STRING << 8u) | (2u * chr_count + 2u));
    return desc_str_buf;
}

void tud_msc_inquiry_cb(uint8_t /*lun*/, uint8_t vendor_id[8],
                         uint8_t product_id[16], uint8_t product_rev[4]) {
    memcpy(vendor_id,   "JLPICART", 8u);
    memcpy(product_id,  "Flash Storage   ", 16u);
    memcpy(product_rev, "1.0 ", 4u);
}

bool tud_msc_test_unit_ready_cb(uint8_t /*lun*/) {
    return true;
}

void tud_msc_capacity_cb(uint8_t /*lun*/,
                          uint32_t* block_count, uint16_t* block_size) {
    *block_count = static_cast<uint32_t>(FLASH_FAT_SIZE / 512u);
    *block_size  = 512u;
}

bool tud_msc_start_stop_cb(uint8_t /*lun*/, uint8_t /*power_condition*/,
                             bool /*start*/, bool /*load_eject*/) {
    return true;
}

bool tud_msc_is_writable_cb(uint8_t /*lun*/) {
    return true;
}

int32_t tud_msc_read10_cb(uint8_t /*lun*/, uint32_t lba, uint32_t /*offset*/,
                           void* buffer, uint32_t bufsize) {
    UINT sectors = static_cast<UINT>(bufsize / 512u);
    DRESULT r = flash_disk_read(1u, static_cast<BYTE*>(buffer), lba, sectors);
    return (r == RES_OK) ? static_cast<int32_t>(bufsize) : -1;
}

int32_t tud_msc_write10_cb(uint8_t /*lun*/, uint32_t lba, uint32_t /*offset*/,
                            uint8_t* buffer, uint32_t bufsize) {
    UINT sectors = static_cast<UINT>(bufsize / 512u);
    DRESULT r = flash_disk_write(1u, static_cast<const BYTE*>(buffer), lba, sectors);
    return (r == RES_OK) ? static_cast<int32_t>(bufsize) : -1;
}

int32_t tud_msc_scsi_cb(uint8_t /*lun*/, uint8_t const /*scsi_cmd*/[16],
                          void* /*buffer*/, uint16_t /*bufsize*/) {
    return -1;  // report ILLEGAL_REQUEST for unrecognised SCSI commands
}

}  // extern "C"

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

[[noreturn]] void UsbDevice::run() {
    log_info("USB config mode: no MSX clock — exposing flash as MSC device");
    tud_init(BOARD_TUD_RHPORT);
    while (true) {
        tud_task();
    }
}

#endif  // !JLPICART_HOST_TEST

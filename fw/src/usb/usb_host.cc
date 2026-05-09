// usb_host.cc — UsbHost implementation.

#include "usb/usb_host.h"
#include "diag/log.h"

bool    UsbHost::g_msc_mounted_  = false;
uint8_t UsbHost::g_msc_dev_addr_ = 0;

bool UsbHost::is_msc_mounted() const { return g_msc_mounted_; }

#ifndef JLPICART_HOST_TEST

#include "tusb.h"
#include <pico/time.h>

void UsbHost::init() {
    tuh_init(BOARD_TUH_RHPORT);
    log_info("USB host initialised");
}

void UsbHost::poll() {
    tuh_task();
}

void UsbHost::on_mount(uint8_t dev_addr) {
    g_msc_dev_addr_ = dev_addr;
    g_msc_mounted_  = true;
    log_info("USB MSC mounted (LUN 0)");
}

void UsbHost::on_umount(uint8_t dev_addr) {
    (void)dev_addr;
    g_msc_mounted_  = false;
    g_msc_dev_addr_ = 0;
    log_info("USB MSC unmounted");
}

// tinyusb MSC host callbacks — called by tuh_task().
extern "C" {

void tuh_msc_mount_cb(uint8_t dev_addr) {
    UsbHost::on_mount(dev_addr);
}

void tuh_msc_umount_cb(uint8_t dev_addr) {
    UsbHost::on_umount(dev_addr);
}

uint32_t tusb_time_millis_api(void) {
    return time_us_64() / 1000;
}

} // extern "C"

#else  // JLPICART_HOST_TEST

void UsbHost::init()  {}
void UsbHost::poll()  {}

void UsbHost::on_mount(uint8_t dev_addr) {
    g_msc_dev_addr_ = dev_addr;
    g_msc_mounted_  = true;
}

void UsbHost::on_umount(uint8_t dev_addr) {
    (void)dev_addr;
    g_msc_mounted_  = false;
    g_msc_dev_addr_ = 0;
}

#endif

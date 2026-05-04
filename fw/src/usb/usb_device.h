#pragma once
// usb_device.h — USB MSC device mode (config/programming mode).
//
// When no MSX clock is detected at boot and secure boot is not enforced,
// the firmware exposes the internal FAT flash partition as a USB mass-storage
// device.  The host PC sees a single 14 MB drive and can install collections.
//
// UsbDevice::run() never returns.  Call fat_vol.unmount() before invoking it.

class UsbDevice {
public:
    [[noreturn]] static void run();
};

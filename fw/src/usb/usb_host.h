#pragma once
// usb_host.h — USB host initialisation and MSC mount tracking.
//
// UsbHost wraps tinyusb host init and task pump.  is_msc_mounted() is
// virtual so that tests can inject a FakeUsbHost without real hardware.
//
// Mount state is updated by tuh_msc_mount_cb / tuh_msc_umount_cb via the
// static on_mount() / on_umount() delegates.

#include <cstdint>

class UsbHost {
public:
    virtual ~UsbHost() = default;

    // Initialise tinyusb host stack (calls tuh_init on BOARD_TUH_RHPORT).
    // No-op in host test builds.
    void init();

    // Drive tinyusb task pump (calls tuh_task).
    // No-op in host test builds.
    void poll();

    // Returns true when a USB MSC device is mounted on LUN 0.
    // Virtual to allow FakeUsbHost in host tests.
    virtual bool is_msc_mounted() const;

    // Called by tuh_msc_mount_cb / tuh_msc_umount_cb to update state.
    static void on_mount(uint8_t dev_addr);
    static void on_umount(uint8_t dev_addr);

private:
    static bool     g_msc_mounted_;
    static uint8_t  g_msc_dev_addr_;   // device address of the mounted MSC
};

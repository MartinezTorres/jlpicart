// hw_probe.cc — Hardware presence probes (Stage 9 gap #2).
//
// Probed capabilities (safe_verify=true in board_descriptor_jlpicart.cc):
//   net.wifi    — ESP32 AT ping on UART0 (GPIO44/45, 115200 baud)
//   ui.oled     — SSD1306 I2C address scan on I2C0 (GPIO37 SDA / GPIO38 SCK)
//   io.usb_host — RP2350 silicon (always present)
//   io.adc      — RP2350 silicon (always present)

#include "spine/hw_probe.h"
#include <cstring>

// ---------------------------------------------------------------------------
// Host-test stub — all hardware is assumed present so that allocator tests
// can exercise the full activation path without real peripherals.
// ---------------------------------------------------------------------------

#ifdef JLPICART_HOST_TEST

bool hw_probe(const char* /*capability_name*/) {
    return true;
}

// ---------------------------------------------------------------------------
// Hardware implementation
// ---------------------------------------------------------------------------

#else

#include "platform/gpio_defs.h"
#include "hardware/uart.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "pico/time.h"

// ---------------------------------------------------------------------------
// probe_net_wifi
//
// Initialises UART0 at 115200, sends "AT\r\n", and looks for "OK" in the
// reply within 1500 ms.  The ESP32 AT firmware responds to "AT" with "OK"
// whether or not it is connected to Wi-Fi.
//
// The UART is left initialised; TransportEspAt::init() will call uart_init()
// again harmlessly when the driver is later brought up.
// ---------------------------------------------------------------------------

static bool probe_net_wifi()
{
    static uart_inst_t* const uart = uart0;

    uart_init(uart, 115200u);
    gpio_set_function(GPIO64_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(GPIO64_UART_RX, GPIO_FUNC_UART);
    uart_set_hw_flow(uart, false, false);
    uart_set_format(uart, 8, 1, UART_PARITY_NONE);

    // Brief drain of startup noise.
    busy_wait_ms(50u);
    while (uart_is_readable(uart)) (void)uart_getc(uart);

    // Send AT command.
    const char cmd[] = "AT\r\n";
    for (size_t i = 0; i < sizeof(cmd) - 1u; ++i)
        uart_putc_raw(uart, cmd[i]);

    // Read lines for up to 1500 ms, looking for "OK".
    uint64_t deadline = time_us_64() + 1500000u;
    char line[16] = {};
    size_t n = 0;
    while (time_us_64() < deadline) {
        if (!uart_is_readable(uart)) continue;
        char c = (char)uart_getc(uart);
        if (c == '\n') {
            if (n == 2u && line[0] == 'O' && line[1] == 'K') return true;
            n = 0;
        } else if (c != '\r') {
            if (n < sizeof(line) - 1u) line[n++] = c;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// probe_ui_oled
//
// Scans I2C0 (100 kHz) at GPIO37 SDA / GPIO38 SCK for an SSD1306 at the
// standard addresses 0x3C and 0x3D.  A zero-byte write is used: the device
// ACKs its own address and the transaction completes without sending any
// data bytes, so the display state is not disturbed.
//
// The I2C peripheral is de-initialised after the probe so that any later
// OLED driver init starts from a clean slate.
// ---------------------------------------------------------------------------

static bool probe_ui_oled()
{
    i2c_init(i2c0, 100000u);
    gpio_set_function(GPIO64_OLEDSDA, GPIO_FUNC_I2C);
    gpio_set_function(GPIO64_OLEDSCK, GPIO_FUNC_I2C);
    gpio_pull_up(GPIO64_OLEDSDA);
    gpio_pull_up(GPIO64_OLEDSCK);

    const uint8_t addrs[] = { 0x3Cu, 0x3Du };
    bool found = false;
    for (uint8_t addr : addrs) {
        // Zero-byte write: sends START + ADDRESS + STOP.
        // Returns >= 0 on ACK, PICO_ERROR_GENERIC on NACK.
        int r = i2c_write_timeout_us(i2c0, addr, nullptr, 0u, false, 5000u);
        if (r >= 0) { found = true; break; }
    }

    i2c_deinit(i2c0);
    return found;
}

// ---------------------------------------------------------------------------

bool hw_probe(const char* name)
{
    if (strcmp(name, "net.wifi") == 0) return probe_net_wifi();
    if (strcmp(name, "ui.oled")  == 0) return probe_ui_oled();
    // io.usb_host and io.adc are RP2350 silicon — always present.
    return true;
}

#endif  // !JLPICART_HOST_TEST

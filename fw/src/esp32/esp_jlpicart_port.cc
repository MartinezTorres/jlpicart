// This file is based on the raspberry pico port fom Expressif Systems under Apache license 

#include <board.h>

static uart_inst_t *s_uart_inst = uart0;


#include "esp_loader_io.h"
#include "hardware/uart.h"

extern "C" {
    esp_loader_error_t loader_port_write(const uint8_t *data, const uint16_t size, const uint32_t timeout);
    esp_loader_error_t loader_port_read(uint8_t *data, const uint16_t size, const uint32_t timeout);
    void loader_port_enter_bootloader(void);
    void loader_port_reset_target(void);
    void loader_port_delay_ms(const uint32_t ms);
    void loader_port_start_timer(const uint32_t ms);
    uint32_t loader_port_remaining_time(void);
    void loader_port_debug_print(const char *str);
    esp_loader_error_t loader_port_change_transmission_rate(const uint32_t baudrate);
}

static uint32_t s_time_end;

esp_loader_error_t loader_port_write(const uint8_t *data, const uint16_t size, const uint32_t) { 

    for (size_t pos = 0; pos < size; pos++) uart_putc_raw(uart0, data[pos]); 
    return ESP_LOADER_SUCCESS; 
}
 
 
esp_loader_error_t loader_port_read(uint8_t *data, const uint16_t size, const uint32_t) {

    for (size_t pos = 0; pos < size; pos++) data[pos] = uart_getc(uart0); 
    return ESP_LOADER_SUCCESS; 
} 
 
void loader_port_enter_bootloader(void) { } // the boot pin isn't available on the jlpicart.
  
void loader_port_reset_target(void) { } // the reset pin isn't available on the jlpicart.
 
void loader_port_delay_ms(const uint32_t ms) { busy_wait_us(ms * uint64_t(1000)); }
 
void loader_port_start_timer(const uint32_t ms) { s_time_end = to_ms_since_boot(get_absolute_time()) + ms; }
 
uint32_t loader_port_remaining_time(void) { if (s_time_end < to_ms_since_boot(get_absolute_time()) ) return 0; else return s_time_end - to_ms_since_boot(get_absolute_time()); }
 
void loader_port_debug_print(const char *str) { DBG::msg<DEBUG_INFO>("ESP32 FLASH DEBUG: ", str); }
 
esp_loader_error_t loader_port_change_transmission_rate(const uint32_t baudrate) { uart_set_baudrate(uart0, baudrate); return ESP_LOADER_SUCCESS; }
 


#include "printf.h"
#include "pico.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/multicore.h"

#define GPIO44_UART_TX 44
#define GPIO45_UART_RX 45

#define GPIO38_OLEDSCK 38

#define UART_HARDWARE
//#define UART_SOFTWARE
//#define UART_SOFTWARE_MULTICORE

#ifdef UART_HARDWARE
static uint8_t init = 0;
void _putchar(char c) {

    if (init == 0) {

        //gpio_set_function(GPIO44_UART_TX, UART_FUNCSEL_NUM(uart0, 0));
        //gpio_set_function(GPIO45_UART_RX, UART_FUNCSEL_NUM(uart0, 1));

        //uart_init(uart0, 115200);
        gpio_set_function(GPIO38_OLEDSCK, GPIO_FUNC_UART_AUX);

        uart_init(uart1, 115200);

        init = 1;
    }

    //uart_putc_raw(uart0, c);
    uart_putc_raw(uart1, c);
}

void _putchar_reset() {
    init = 0;
}
#endif


#ifdef UART_SOFTWARE_MULTICORE

#define BUFFER_SIZE (128*1024)
static uint16_t pa, pb;
static uint8_t pbuffer[BUFFER_SIZE]; 
static void putchar_core1_entry() {
    
    while (true) {
        if (pa==pb) {
            busy_wait_us_32(26);
            continue;
        }
    
        uint8_t character = pbuffer[pa++];
        if (pa == BUFFER_SIZE) pa = 0;

        busy_wait_us_32(26); gpio_put(UART_TX_PIN, false);
        busy_wait_us_32(26); gpio_put(UART_TX_PIN, character & 0x01);
        busy_wait_us_32(26); gpio_put(UART_TX_PIN, character & 0x02);
        busy_wait_us_32(26); gpio_put(UART_TX_PIN, character & 0x04);
        busy_wait_us_32(26); gpio_put(UART_TX_PIN, character & 0x08);
        busy_wait_us_32(26); gpio_put(UART_TX_PIN, character & 0x10);
        busy_wait_us_32(26); gpio_put(UART_TX_PIN, character & 0x20);
        busy_wait_us_32(26); gpio_put(UART_TX_PIN, character & 0x40);
        busy_wait_us_32(26); gpio_put(UART_TX_PIN, character & 0x80);
        busy_wait_us_32(26); gpio_put(UART_TX_PIN, true);
        busy_wait_us_32(26);
    }

}

static inline void putchar_init() {

    static uint8_t init = 0;
    if (init != 0) return;

    init = 1;

    gpio_init(UART_TX_PIN);
    gpio_set_dir(UART_TX_PIN, GPIO_OUT);
    gpio_put(UART_TX_PIN, true);
    busy_wait_us_32(10000); 

    pa = 0;
    pb = 0;

    multicore_launch_core1(putchar_core1_entry);
}

void _putchar(char c) {

    putchar_init();
    uint16_t pbn = (pb+1) & (BUFFER_SIZE-1);
    if (pbn != pa) {
        pbuffer[pb] = c;
        pb = pbn;
    }
}
#endif


#ifdef UART_SOFTWARE
void _putchar(char c) {

    uint8_t character = c;

    static uint8_t init = 0;
    if (init == 0) {

        gpio_init(UART_TX_PIN);
        gpio_set_dir(UART_TX_PIN, GPIO_OUT);
        gpio_put(UART_TX_PIN, true);
        busy_wait_us_32(10000); 

        init = 1;
    }

    busy_wait_us_32(26); gpio_put(UART_TX_PIN, false);
    busy_wait_us_32(26); gpio_put(UART_TX_PIN, character & 0x01);
    busy_wait_us_32(26); gpio_put(UART_TX_PIN, character & 0x02);
    busy_wait_us_32(26); gpio_put(UART_TX_PIN, character & 0x04);
    busy_wait_us_32(26); gpio_put(UART_TX_PIN, character & 0x08);
    busy_wait_us_32(26); gpio_put(UART_TX_PIN, character & 0x10);
    busy_wait_us_32(26); gpio_put(UART_TX_PIN, character & 0x20);
    busy_wait_us_32(26); gpio_put(UART_TX_PIN, character & 0x40);
    busy_wait_us_32(26); gpio_put(UART_TX_PIN, character & 0x80);
    busy_wait_us_32(26); gpio_put(UART_TX_PIN, true);
    busy_wait_us_32(26);
}
#endif



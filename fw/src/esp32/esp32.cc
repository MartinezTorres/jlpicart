#include <board.h>
#include "esp32.h"

#include <multitask/multitask.h>

static char buffer[512];

namespace ESP32 {

    Multitask::CallAgain task() {

        if (uart_is_readable(uart0)) {
            DBG::putchar<DEBUG_BLOCKING_ENABLED>(uart_getc(uart0));
        }



        return Multitask::CALL_AGAIN;
    }

    void init() {

        gpio_set_function(GPIO64_UART_TX, UART_FUNCSEL_NUM(uart0, GPIO64_UART_TX));
        gpio_set_function(GPIO64_UART_RX, UART_FUNCSEL_NUM(uart0, GPIO64_UART_RX));
/*        
        DBG::msg<DEBUG_INFO>("INIT ESP32");
        //uart_puts(uart0, "AT+RST\r\n");
        
//        uart_init(uart0, 922190);
//        uart_puts(uart0, "AT+UART_CUR=115200,8,1,0,0\r\n");
        uart_init(uart0, 115200);
        //uart_puts(uart0, "AT+UART_CUR=115200,8,1,0,0\r\n");
        //busy_wait_us(100000);
        uart_puts(uart0, "AT+UART_CUR?\r\n");
        while(true) {
            int i = 0;
            for (i = 0; i<511 ; i++) {
                
                if (uart_is_readable(uart0)) {
                    buffer[i] = uart_getc(uart0);
                    continue;
                }
                busy_wait_us(10);
                if (not uart_is_readable(uart0)) break;
            }
            for (int j = 0; j < i; j++) DBG::putchar<DEBUG_BLOCKING_ENABLED>(buffer[j]);
            if (i>0) break;
        }
        uart_puts(uart0, "AT+GMR\r\n");
        while(true) {
            int i = 0;
            for (i = 0; i<511 ; i++) {
                
                if (uart_is_readable(uart0)) {
                    buffer[i] = uart_getc(uart0);
                    continue;
                }
                busy_wait_us(100);
                if (not uart_is_readable(uart0)) break;
            }
            for (int j = 0; j < i; j++) DBG::putchar<DEBUG_BLOCKING_ENABLED>(buffer[j]);
            //if (i>0) break;
        }

        for (int t = 0; t < 20; t++) {
            int i = 0;
            for (i = 0; i<511 and uart_is_readable(uart0); i++) {
                buffer[i] = uart_getc(uart0);
            }

            DBG::msg<DEBUG_INFO>("GOT ", uint16_t(i), " bytes");
            for (int j = 0; j < i; j++) {
                DBG::msg<DEBUG_INFO>("BYTE: ", uint16_t(i), ": ", buffer[i]);
            }
            DBG::msg<DEBUG_INFO>("\nMESSAGE:");   
            for (int j = 0; j < i; j++) DBG::putchar<DEBUG_BLOCKING_ENABLED>(buffer[i]);
            DBG::msg<DEBUG_INFO>("\n END OF MESSAGE");   
        }
        DBG::msg<DEBUG_INFO>("ESP32 SEND RESET");
//        uart_puts(uart0, "AT+RST\r\n");
//        busy_wait_us(500 * 1000);
//        while (uart_is_readable(uart0)) {
//            DBG::putchar<DEBUG_BLOCKING_ENABLED>(uart_getc(uart0));
//        }
        DBG::msg<DEBUG_INFO>("ESP32 DONE");*/


    }

}


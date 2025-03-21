#include <board.h>
#include "esp32.h"

#include <multitask/multitask.h>

#include "generated_binaries/binaries.h"

#define MD5_ENABLED 0

#include "esp_loader.h"



namespace ESP32 {

    /*Multitask::CallAgain task() {

        if (uart_is_readable(uart0)) {
            DBG::putchar<DEBUG_BLOCKING_ENABLED>(uart_getc(uart0));
        }



        return Multitask::CALL_AGAIN;
    }*/


    alignas(1<<13) static uint8_t buffer[1<<13];

    static int dma_channel_read;
    static int dma_channel_write;

    


    static esp_loader_error_t flash_binary(const uint8_t *bin_addr, size_t size, size_t address = 0) {
        

        auto get_error_string = [](esp_loader_error_t error_code) {
    
            const char *error_codes[] = {
                "NONE", "UNKNOWN", "TIMEOUT", "IMAGE SIZE","INVALID MD5", 
                "INVALID PARAMETER", "INVALID TARGET", "UNSUPPORTED CHIP", 
                "UNSUPPORTED FUNCTION", "INVALID RESPONSE", "WRONG ERROR CODE",
            };

            esp_loader_error_t last_error_code = esp_loader_error_t(sizeof(error_codes)/sizeof(error_codes[0]) - 1);
        
            return error_codes[std::min(error_code, last_error_code)];
        };
        size_t payload_size = 1024;

        //printf("Erasing flash (this may take a while)...\n");
        esp_loader_error_t err = esp_loader_flash_start(address, size, payload_size);
        if (err != ESP_LOADER_SUCCESS) {
            //printf("Erasing flash failed with error: %s.\n", get_error_string(err));
    
            if (err == ESP_LOADER_ERROR_INVALID_PARAM) {
                //printf("If using Secure Download Mode, double check that the specified target flash size is correct.\n");
            }
            return err;
        }
        //printf("Start programming\n");
    
        size_t binary_size = size;
        size_t written = 0;
    
        while (size > 0) {
            size_t to_read = std::min(size, payload_size);
            //memcpy(payload, bin_addr, to_read);
            for (size_t i=0; i<to_read; i++) buffer[i] = bin_addr[i];
    
            err = esp_loader_flash_write(buffer, to_read);
            if (err != ESP_LOADER_SUCCESS) {
                //printf("\nPacket could not be written! Error %s.\n", get_error_string(err));
                return err;
            }
    
            size -= to_read;
            bin_addr += to_read;
            written += to_read;
    
            int progress = (int)(((float)written / binary_size) * 100);
        };
    
        return ESP_LOADER_SUCCESS;
    }

    static void init_dma() {


        {
            dma_channel_read = dma_claim_unused_channel(true);
            dma_channel_config c = dma_channel_get_default_config(dma_channel_read);

            channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
            channel_config_set_dreq(&c, DREQ_UART0_RX);

            channel_config_set_read_increment(&c, false);
            channel_config_set_ring(&c, true, 13);
            channel_config_set_write_increment(&c, true);

            dma_channel_configure( dma_channel_read, &c,
                buffer,
                &uart0_hw->dr,
                0xFFFFFFFFU,   // This sets mode to endless transfers
                true           // Start immediately.
            );
        }

        {
            dma_channel_write = dma_claim_unused_channel(true);
            dma_channel_config c = dma_channel_get_default_config(dma_channel_write);

            channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
            channel_config_set_dreq(&c, DREQ_UART0_TX);

            channel_config_set_read_increment(&c, true);
            channel_config_set_write_increment(&c, false);

            dma_channel_configure( dma_channel_write, &c,
                &uart0_hw->dr,
                nullptr,
                0,
                false           // Don't start yet.
            );
        }


    }
    
    static void send_cmd(const char *msg, size_t sz) {

        dma_channel_transfer_from_buffer_now(dma_channel_write, msg, sz);
    }

    static void debug_cmd(const char *msg, const char *OK = "OK\r\n") {

        size_t pt_begin = ((uint8_t *)dma_channel_hw_addr(dma_channel_read)->write_addr) - &buffer[0];

        size_t msg_sz = 0;
        for (size_t i = 0; msg[i] != 0 and i < 8192; i++) msg_sz++;
        send_cmd(msg, msg_sz);

        int wait_counts = 0;
        int status = 0;
        while (wait_counts < 10000) {

            size_t pt_current = ((uint8_t *)dma_channel_hw_addr(dma_channel_read)->write_addr) - &buffer[0];

            if ( pt_current == pt_begin ) {
                busy_wait_us(1000);
                wait_counts++;
            } else {
                char c = buffer[pt_begin++];
                if (pt_begin == sizeof(buffer)) pt_begin = 0;
                wait_counts = 0;

                DBG::putchar<DEBUG_BLOCKING_ENABLED>(c);
                if (OK[0] != 0) {
                    if ( OK[status] == c) { 
                        status++;
                    } else {
                        status = 0;
                    }
                    if ( OK[status] == 0) break;
                }
            }
        }
    }

    void init() {

        gpio_set_function(GPIO64_UART_TX, UART_FUNCSEL_NUM(uart0, GPIO64_UART_TX));
        gpio_set_function(GPIO64_UART_RX, UART_FUNCSEL_NUM(uart0, GPIO64_UART_RX));
        
        DBG::msg<DEBUG_INFO>("INIT ESP32");

        uart_init(uart0, 115200);

        init_dma();

        debug_cmd( "AT+RST\r\n", "ready\r\n" ); 

        debug_cmd( "AT+UART_CUR=1500000,8,1,0,0\r\n" ); 
        uart_init(uart0, 1500000); 
        debug_cmd( "AT+UART_CUR?\r\n" );
        debug_cmd( "AT+GMR\r\n" );

        debug_cmd( "AT+CWINIT=1\r\n" );
        debug_cmd( "AT+CWMODE=1\r\n" );
        debug_cmd( "AT+CWJAP=\"access_point\",\"password\"\r\n" );
        debug_cmd( "AT+CIPMUX=1\r\n" );
        debug_cmd( "AT+CIPSTART=0,\"TCP\",\"jlpicart.servegame.com\",3344\r\n" );
        debug_cmd( "AT+CIPSEND=0,12\r\n" );
        debug_cmd( "HELLO THERE\n" );
        debug_cmd( "AT+CIPSEND=0,16\r\n" );
        debug_cmd( "GENERAL KENOBI!\n" );
        
        
        



        debug_cmd( "AT+UART_CUR=921600,8,1,0,0\r\n" ); 
        uart_init(uart0, 921600); 
        debug_cmd( "AT+UART_CUR?\r\n" );
        debug_cmd( "AT+GMR\r\n" );

        debug_cmd( "AT+UART_CUR=115200,8,1,0,0\r\n" ); 
        uart_init(uart0, 115200); 
        debug_cmd( "AT+UART_CUR?\r\n" );
        debug_cmd( "AT+GMR\r\n" );

        //flash_binary(esp32_at_bin, sizeof(esp32_at_bin));

        while (true);


        DBG::msg<DEBUG_INFO>("ESP32 DONE");


    }


    
}

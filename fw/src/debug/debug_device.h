#pragma once
#include <board.h>

enum DebugLevel : int {
    DEBUG_INFO = 0,
    DEBUG_WARNING = 1,
    DEBUG_ERROR = 2,
};

enum DebugDeviceType {
    DEBUG_DEVICE_DISABLED,
    DEBUG_DEVICE_GPIO44_UART_TX,
    DEBUG_DEVICE_GPIO38_OLEDSCK,
};

enum DebugBlocking {
    DEBUG_BLOCKING_DISABLED,
    DEBUG_BLOCKING_ENABLED,
};

template<DebugLevel MinimumLevel, DebugDeviceType DeviceType>
struct DebugDevice {
    DebugDevice() = delete;

    template<DebugBlocking BlockingStatus>
    static inline void putchar(char c) {
        if constexpr (BlockingStatus == DEBUG_BLOCKING_DISABLED and DeviceType == DEBUG_DEVICE_GPIO44_UART_TX) uart_get_hw(uart0)->dr = c;
        if constexpr (BlockingStatus == DEBUG_BLOCKING_DISABLED and DeviceType == DEBUG_DEVICE_GPIO38_OLEDSCK) uart_get_hw(uart1)->dr = c;
        if constexpr (BlockingStatus == DEBUG_BLOCKING_ENABLED  and DeviceType == DEBUG_DEVICE_GPIO44_UART_TX) uart_putc_raw(uart0, c);
        if constexpr (BlockingStatus == DEBUG_BLOCKING_ENABLED  and DeviceType == DEBUG_DEVICE_GPIO38_OLEDSCK) uart_putc_raw(uart1, c);
    }

    static void init() {

        if constexpr (DeviceType == DEBUG_DEVICE_GPIO44_UART_TX) {
            gpio_set_function(GPIO64_UART_TX, GPIO_FUNC_UART);
            uart_init(uart0, 115200);
        }
        if constexpr (DeviceType == DEBUG_DEVICE_GPIO38_OLEDSCK) {
            gpio_set_function(GPIO64_OLEDSCK, GPIO_FUNC_UART_AUX);
            uart_init(uart1, 115200);
        }
    }

    template<DebugBlocking BlockingStatus>
    static void msg_send_prefix() {}

    template<DebugLevel MsgLevel, DebugBlocking BlockingStatus = DEBUG_BLOCKING_ENABLED, typename... Args>
    static void msgf(const char* format, Args... args) {

        if (MsgLevel<MinimumLevel) return;
        msg_send_prefix<BlockingStatus>();
        fctprintf([](char c, void *){putchar<BlockingStatus>(c); }, nullptr, format, args...);
        putchar<BlockingStatus>('\n');
    }

    template<DebugLevel MsgLevel, DebugBlocking BlockingStatus = DEBUG_BLOCKING_ENABLED, typename... Args>
    static void msg(Args&&... args) {

        if (MsgLevel<MinimumLevel) return;
        msg_send_prefix<BlockingStatus>();
        (put<BlockingStatus>(std::forward<Args>(args)), ...);
        putchar<BlockingStatus>('\n');
    }

    template<DebugBlocking BlockingStatus = DEBUG_BLOCKING_ENABLED>
    static void put(const char *s) { while (*s) putchar<BlockingStatus>(*s++); }

    template<DebugBlocking BlockingStatus = DEBUG_BLOCKING_ENABLED>
    static void put(int a) { 
        if (a == 0) { putchar<BlockingStatus>('0'); return; }
        if (a < 0)  { putchar<BlockingStatus>('-'); a = -a; }
        int sz = 0;
        static char rnum[40];
        while (a) { rnum[sz++] = (a % 10); a /= 10; }
        while (sz) putchar<BlockingStatus>(rnum[--sz]);
    }

    template <DebugBlocking BlockingStatus = DEBUG_BLOCKING_ENABLED, typename T>
    static typename std::enable_if<std::is_unsigned<T>::value>::type
    put(T a) {
        constexpr int num_nibbles = sizeof(T) * 2;
        for (int i = num_nibbles - 1; i >= 0; --i) 
            putchar<BlockingStatus>("0123456789ABCDEF"[(a >> (i * 4)) & 0xF]);        
        putchar<BlockingStatus>('h');
    }
};


#pragma once

namespace SSD1306 {

    void init();

    void enable_display(bool on);

    void set_display_start_line(uint8_t line);

    void set_invert(bool active);

    void putchar(uint8_t idx);

    void locate(uint8_t x, uint8_t y);

    void puts(const char *str);
}
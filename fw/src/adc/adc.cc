#include "adc.h"

namespace ADC {

    uint16_t get_temp() {

        adc_select_input(8);
        const float conversion_factor = 3.3f / (1 << 12);
        const float voltage = adc_read() * conversion_factor;
        const float temp = 27 - (voltage-0.706) / 0.001721;
        return uint16_t(temp*10.+0.5);
    }

    uint16_t get_vbat() {

        adc_select_input(7);
        const int conversion_factor = (3300 << 16) / (1 << 12);
        return (adc_read() * conversion_factor) >> 15;   
    }
}
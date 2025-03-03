Sources:

ESP32C3-AT firmware:
https://github.com/espressif/esp-at/releases/tag/v3.3.0.0#ESP32C3-AT


ESP-SERIAL-FLASHER:
https://github.com/espressif/esp-serial-flasher/tree/v1.8.0

AT.PY TOOL:
https://docs.espressif.com/projects/esp-at/en/release-v3.3.0.0/esp32c3/Compile_and_Develop/tools_at_py.html
https://github.com/espressif/esp-at/blob/498f10ca/tools/at.py

python3 at.py modify_bin --baud 115200 --tx_pin 21 --rx_pin 20 --cts_pin -1 --rts_pin -1  --input factory_MINI-1.bin 


TINY-USB: 
modified from pico-sdk

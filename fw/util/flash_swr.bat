echo "Copying file"

scp build\jlpicart.elf 192.168.17.205:~/main.elf

C:\\Users\\Manel\\.pico-sdk\\toolchain\\13_3_Rel1\\bin\\arm-none-eabi-gdb -batch  -ex "set remotetimeout 20" -ex "target remote 192.168.17.205:3333" -ex "set remotetimeout 20" -ex "monitor program /home/manel/main.elf verify reset" -ex "quit"
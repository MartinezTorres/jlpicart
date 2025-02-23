echo "Copying file"

scp c:\Users\Manel\Desktop\PicoTest\megapico\sw\build\picocd.elf 192.168.17.205:~/main.elf

C:\\Users\\Manel\\.pico-sdk\\toolchain\\13_3_Rel1\\bin\\arm-none-eabi-gdb -batch -ex "target remote 192.168.17.205:3333" -ex "monitor program /home/manel/main.elf verify reset" -ex "quit"
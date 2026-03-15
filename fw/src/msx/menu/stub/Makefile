# Makefile — build the Z80 menu stub with SDCC.
#
# Invoke from fw/ root:  make -C src/msx/menu/stub
# Or via CMake custom target build_menu_stub.
#
# Outputs:
#   stub.bin   — raw binary (max 2048 bytes), placed at ROM offset MENU_STUB_OFS
#   menupage.rom — 16 KB page image for openMSX fixtures (Stage 12)

SDCC     := $(or $(shell which sdcc 2>/dev/null),fw/tools/sdcc/bin/sdcc)
# sdasz80 lives next to sdcc; derive path so both pinned and system installs work.
SDASZ80  := $(dir $(SDCC))sdasz80
OBJCOPY  := objcopy
PYTHON3  := python3

# --code-loc 0x7800: places _CODE at Z80 0x7800 (= MENU_STUB_OFS + PAGE_BASE).
# --data-loc 0x7700: places the DATA segment (static vars) at 0x7700, within
#                    the menu page and below the stack (SP=0x77FE). Without
#                    this, SDCC defaults to 0x0000, outside our mapped page.
# --no-std-crt0:     use our own crt0.s.
# --stack-auto:      no static overlay (safer; avoids linker complaints).
# --out-fmt-ihx:     Intel HEX output.
SDCC_FLAGS := -mz80 --no-std-crt0 --code-loc 0x7800 --data-loc 0x7700 \
              --stack-auto --out-fmt-ihx

.PHONY: all clean

all: stub.bin menupage.rom

# SDCC 4.2+ does not accept .s files via the sdcc driver; assemble separately.
crt0.rel: crt0.s
	$(SDASZ80) -o crt0.rel crt0.s

stub.ihx: crt0.rel stub.c stub_bios.h
	$(SDCC) $(SDCC_FLAGS) crt0.rel stub.c -o stub.ihx

stub.bin: stub.ihx
	$(OBJCOPY) -I ihex -O binary stub.ihx stub.bin
	@SIZE=$$(wc -c < stub.bin); \
	echo "stub.bin: $$SIZE bytes (max 2048)"; \
	if [ "$$SIZE" -gt 2048 ]; then \
	    echo "ERROR: stub exceeds 2 KB limit"; exit 1; \
	fi

# 16 KB page image: header + mailbox (zeroed) + data buffer (zeroed) + stub.
# Header and mailbox are zeroed here; RP2350 fills them at runtime via init().
# openMSX loads this ROM image directly into the cartridge slot.
#
# SDCC places INITIALIZED (static var storage) at 0x7700 and CODE at 0x7800.
# The IHX output from SDCC includes one INITIALIZER byte at 0x7701 (the initial
# value of g_mode), so objcopy starts the binary at Z80 0x7701, not 0x7700.
# Place stub.bin at ROM offset 0x3701 so that the binary lands at Z80 0x7701
# as required (CODE then starts at Z80 0x7800 with all internal addresses
# correct).  The INITIALIZED byte for g_mode at Z80 0x7700 = ROM[0x3700] is
# left as 0x00 by the zeroed page initialisation, which is the correct default.
# __start from crt0.s is not necessarily at 0x7800; read its address from the
# map file so the ROM INIT vector and bootstrap JP use the correct entry point.
menupage.rom: stub.bin stub.map
	$(PYTHON3) -c "\
import re; \
stub = open('stub.bin','rb').read(); \
map_txt = open('stub.map').read(); \
m = re.search(r'([0-9A-Fa-f]{8})\s+__start', map_txt); \
assert m, '__start not found in stub.map'; \
entry = int(m.group(1), 16); \
assert 0x7800 <= entry <= 0x7FFF, 'entry 0x%04X out of range' % entry; \
assert len(stub) <= 0x0900, 'stub %d B exceeds 2304 B' % len(stub); \
page = bytearray(0x4000); \
page[0x3701:0x3701+len(stub)] = stub; \
page[0] = 0x41; page[1] = 0x42; \
page[2] = entry & 0xFF; page[3] = (entry >> 8) & 0xFF; \
open('menupage.rom','wb').write(bytes(page)); \
print('  __start=0x%04X  stub=%dB' % (entry, len(stub)))"
	@echo "menupage.rom: 16384 bytes"

clean:
	rm -f stub.ihx stub.bin stub.lst stub.map stub.noi menupage.rom \
	      stub.asm crt0.rel crt0.lst crt0.asm crt0.sym

# Makefile — JLPiCart Z80 Menu Stub (standalone ROM)
#
# Uses pinned SDCC 4.5.0 from ext/sdcc/bin/.
# Run from fw/src/msx/menu/standalone/ or via 'make -C fw/src/msx/menu/standalone' from fw/.
#
# Output: menu_stub.rom — flat binary, 16KB, page-relative (0x0000–0x3FFF).

TOOLS_DIR := ../../../../ext/sdcc
SDCC      := $(TOOLS_DIR)/bin/sdcc
SDASZ80   := $(TOOLS_DIR)/bin/sdasz80

TARGET    := menu_stub

# SDCC flags for the menu stub.
# --code-loc 0x4110: _CODE starts after the ABS-placed stub_entry block at 0x4100.
#   (stub_entry + halt = ~9 bytes; 0x4110 provides safe headroom.)
# --data-loc 0xE000: variables in upper 8KB RAM (safe on MSX1).
# --no-std-crt0:     we supply our own crt0.s.
SDCC_FLAGS  := -mz80 --code-loc 0x4110 --data-loc 0xE000 --no-std-crt0 \
               --out-fmt-ihx
SDAS_FLAGS  := -plosgff

SRCS   := main.c
ASRCS  := crt0.s
RELS   := crt0.rel main.rel

.PHONY: all clean

all: $(TARGET).rom

# Assemble crt0.s.
crt0.rel: crt0.s
	$(SDASZ80) $(SDAS_FLAGS) $@ $<

# Compile main.c (SDCC -c produces a .rel file).
main.rel: main.c
	$(SDCC) -mz80 -c $< -o $@

# Link: crt0.rel must be first so the ABS section lands at the right addresses.
$(TARGET).ihx: $(RELS)
	$(SDCC) $(SDCC_FLAGS) $(RELS) -o $@

# Convert Intel Hex to flat binary and pad to exactly 16KB.
# --change-address -0x4000 converts absolute Z80 addresses to page-relative.
$(TARGET).rom: $(TARGET).ihx
	objcopy --input-target ihex --output-target binary \
	    --change-address -0x4000 $< $@
	@size=$$(wc -c < $@); \
	 if [ $$size -lt 16384 ]; then \
	     dd if=/dev/zero bs=1 count=$$((16384 - $$size)) >> $@ 2>/dev/null; \
	 fi

clean:
	rm -f *.rel *.asm *.lst *.sym *.map *.lk *.noi *.mem *.ihx *.rom

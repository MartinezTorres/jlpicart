# Makefile — JLPiCart Z80 Menu Stub (standalone ROM)
#
# Uses pinned SDCC 4.5.0 from ext/bin/sdcc/bin/.
# Run from fw/src/msx/menu/standalone/ or via 'make -C fw/src/msx/menu/standalone'.
#
# Output: menu_stub.rom — flat binary, 16KB, page-relative (0x0000–0x3FFF).
# When BUILD_DIR is set, all artifacts go there instead of the source directory.

SDCC      := $(or $(SDCC),$(abspath $(CURDIR)/../../../../ext/bin/sdcc/bin/sdcc))
SDASZ80   := $(dir $(SDCC))sdasz80

SRCDIR    := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
OUT       := $(if $(BUILD_DIR),$(BUILD_DIR)/,)

TARGET    := menu_stub

# SDCC flags for the menu stub.
# --code-loc 0x4110: _CODE starts after the ABS-placed stub_entry block at 0x4100.
#   (stub_entry + halt = ~9 bytes; 0x4110 provides safe headroom.)
# --data-loc 0xE000: variables in upper 8KB RAM (safe on MSX1).
# --no-std-crt0:     we supply our own crt0.s.
SDCC_FLAGS  := -mz80 --code-loc 0x4110 --data-loc 0xE000 --no-std-crt0 \
               --out-fmt-ihx
SDAS_FLAGS  := -plosgff

.PHONY: all clean

all: $(OUT)$(TARGET).rom

# Assemble crt0.s.
$(OUT)crt0.rel: $(SRCDIR)/crt0.s
	@mkdir -p $(OUT)
	$(SDASZ80) $(SDAS_FLAGS) $@ $<

# Compile main.c (SDCC -c produces a .rel file).
$(OUT)main.rel: $(SRCDIR)/main.c
	@mkdir -p $(OUT)
	$(SDCC) -mz80 -c $< -o $@

# Link: crt0.rel must be first so the ABS section lands at the right addresses.
$(OUT)$(TARGET).ihx: $(OUT)crt0.rel $(OUT)main.rel
	@mkdir -p $(OUT)
	$(SDCC) $(SDCC_FLAGS) $(OUT)crt0.rel $(OUT)main.rel -o $@

# Convert Intel Hex to flat binary and pad to exactly 16KB.
# --change-address -0x4000 converts absolute Z80 addresses to page-relative.
$(OUT)$(TARGET).rom: $(OUT)$(TARGET).ihx
	@mkdir -p $(OUT)
	objcopy --input-target ihex --output-target binary \
	    --change-address -0x4000 $< $@
	@size=$$(wc -c < $@); \
	 if [ $$size -lt 16384 ]; then \
	     dd if=/dev/zero bs=1 count=$$((16384 - $$size)) >> $@ 2>/dev/null; \
	fi

clean:
	@if [ -n "$(OUT)" ]; then \
		rm -rf "$(OUT)"; \
	else \
		rm -f *.rel *.asm *.lst *.sym *.map *.lk *.noi *.mem *.ihx *.rom; \
	fi

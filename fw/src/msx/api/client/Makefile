# Makefile — JLPiCart Z80 API reference client
#
# Uses pinned SDCC 4.5.0 from ext/bin/sdcc/bin/.
# Run from fw/src/msx/api/client/ or via 'make -C fw/src/msx/api/client'.
#
# Output: api_client.rom — flat binary, loadable as a 16KB MSX page-1 ROM.
# When BUILD_DIR is set, all artifacts go there instead of the source directory.

SDCC      := $(or $(SDCC),$(abspath $(CURDIR)/../../../../ext/bin/sdcc/bin/sdcc))
SDASZ80   := $(dir $(SDCC))sdasz80

SRCDIR    := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
OUT       := $(if $(BUILD_DIR),$(BUILD_DIR)/,)

TARGET    := api_client

# SDCC flags for MSX Z80 ROM at page 1 (0x4000).
# --code-loc 0x4010: _CODE starts after the 16-byte ROM header.
# --data-loc 0xE000: variables in upper 8KB RAM (safe on MSX1).
# --no-std-crt0:     we supply our own crt0.s.
SDCC_FLAGS  := -mz80 --code-loc 0x4010 --data-loc 0xE000 --no-std-crt0 \
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

# Link: crt0.rel must be first so the header lands at 0x4000.
$(OUT)$(TARGET).ihx: $(OUT)crt0.rel $(OUT)main.rel
	@mkdir -p $(OUT)
	$(SDCC) $(SDCC_FLAGS) $(OUT)crt0.rel $(OUT)main.rel -o $@

# Convert Intel Hex to flat binary ROM image.
$(OUT)$(TARGET).rom: $(OUT)$(TARGET).ihx
	@mkdir -p $(OUT)
	objcopy --input-target ihex --output-target binary \
	    --change-address -0x4000 $< $@

clean:
	@if [ -n "$(OUT)" ]; then \
		rm -rf "$(OUT)"; \
	else \
		rm -f *.rel *.asm *.lst *.sym *.map *.lk *.noi *.mem *.ihx *.rom; \
	fi

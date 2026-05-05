# Makefile — JLPiCart Z80 API reference client
#
# Uses pinned SDCC 4.5.0 from ext/sdcc/bin/.
# Run from fw/src/msx/api/client/ or via 'make -C fw/src/msx/api/client' from fw/.
#
# Output: api_client.rom — flat binary, loadable as a 16KB MSX page-1 ROM.

TOOLS_DIR := ../../../../ext/sdcc
SDCC      := $(TOOLS_DIR)/bin/sdcc
SDASZ80   := $(TOOLS_DIR)/bin/sdasz80

TARGET    := api_client

# SDCC flags for MSX Z80 ROM at page 1 (0x4000).
# --code-loc 0x4010: _CODE starts after the 16-byte ROM header.
# --data-loc 0xE000: variables in upper 8KB RAM (safe on MSX1).
# --no-std-crt0:     we supply our own crt0.s.
SDCC_FLAGS  := -mz80 --code-loc 0x4010 --data-loc 0xE000 --no-std-crt0 \
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

# Link: crt0.rel must be first so the header lands at 0x4000.
$(TARGET).ihx: $(RELS)
	$(SDCC) $(SDCC_FLAGS) $(RELS) -o $@

# Convert Intel Hex to flat binary ROM image.
$(TARGET).rom: $(TARGET).ihx
	objcopy --input-target ihex --output-target binary \
	    --change-address -0x4000 $< $@

clean:
	rm -f *.rel *.asm *.lst *.sym *.map *.lk *.noi *.mem *.ihx *.rom

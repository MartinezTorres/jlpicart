.PHONY: all clean
.SECONDARY:  

all: z80stub.rom 

# ASM
ASM = sdasz80
ASM_FLAGS = -ogslp

MSG = "\033[1;32m[$(@)]\033[1;31m\033[0m"

###### SDCC NATIVE: ASM
%.rel: %.s
	@echo -n $(MSG)
	@$(ASM) $(ASM_FLAGS) $@ $<
	@echo " DONE"
	
%: %.cc
	@echo -n $(MSG)
	@g++ -o $@ $<
	@echo " DONE"

z80stub.rom: megalinker z80stub.rel
	./megalinker z80stub.rom z80stub.rel
	xxd -c 16 -i z80stub.rom

clean:
	@echo -n "Cleaning... "
	@rm -rf *.lst *.rel *.sym megalinker *.rom *.map
	@echo "Done!"

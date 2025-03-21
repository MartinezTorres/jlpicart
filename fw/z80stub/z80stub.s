    .module z80_stub

.globl  ___ML_CONFIG_RAM_START
.globl  ___ML_CONFIG_INIT_ROM_START
.globl  ___ML_CONFIG_INIT_RAM_START
.globl  ___ML_CONFIG_INIT_SIZE

___ML_CONFIG_RAM_START  =   0xE000

EXEC_START =   0x4030
WRITE_MAPPED_ADDRESS =   0x7FFF

;--------------------------------------------------------
; HEADER
;--------------------------------------------------------

.area _HEADER (ABS)
; Reset vector
    .org 0x4000
    .db  0x41
    .db  0x42
    .dw  init
    .dw  0x0000
    .dw  0x0000
    .dw  0x0000
    .dw  0x0000
    .dw  0x0000
    .dw  0x0000

boot_trampoline:
    ld hl, (#0x4002)
    ld a, h
    or a
    jr z, boot_trampoline
    jp (hl)

init:

    di

    ld de, #___ML_CONFIG_INIT_RAM_START
    ld hl, #boot_trampoline
    ld bc, #(init - boot_trampoline)
	ldir

    ld de, #WRITE_MAPPED_ADDRESS
    ld hl, #EXEC_START
    jp (hl)


    




    

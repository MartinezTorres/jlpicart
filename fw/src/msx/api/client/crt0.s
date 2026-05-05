; crt0.s — minimal MSX ROM startup for JLPiCart Z80 API client.
;
; Provides the MSX ROM header at 0x4000 and calls _main.
; Built with pinned SDCC 4.5.0 assembler (sdasz80).
;
; MSX ROM slot 1, page 1 (0x4000–0x7FFF).
; Stack is set in upper RAM (0xF380) which is safe on MSX1 and later.

        .module crt0
        .globl  _main

        .area   _HEADER (ABS)
        .org    0x4000

        ; MSX ROM identification header.
        .db     0x41, 0x42      ; 'AB' — MSX ROM magic
        .dw     _rom_init       ; INIT routine address
        .dw     0x0000          ; STATEMENT entry (not used)
        .dw     0x0000          ; DEVICE entry (not used)
        .dw     0x0000          ; TEXT entry (not used)
        .db     0, 0, 0, 0, 0, 0 ; reserved (must be zero)

_rom_init:
        ld      sp, #0xF380     ; stack below BIOS workspace
        call    _main           ; run the C program
        ret                     ; return to BIOS slot scanner

        .area   _CODE
        .area   _DATA
        .area   _BSS

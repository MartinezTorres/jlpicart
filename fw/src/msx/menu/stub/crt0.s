; crt0.s — minimal Z80 startup for the JLPiCart menu stub.
;
; The stub is compiled with --code-loc 0x7800, placing _CODE at Z80 address
; 0x7800 (page 1 base 0x4000 + MENU_STUB_OFS 0x3800).  This startup code is
; linked FIRST (crt0.s appears before stub.c on the SDCC command line), so it
; occupies the first bytes of the code region at 0x7800.
;
; Stack: set to 0x77FE (top of the usable data buffer area, 2 bytes below
; MENU_STUB_OFS).  The data exchange area starts at 0x4100 (low end of buffer)
; so there is ~14.5 KB between data and stack — no overlap for a simple stub.

        .module crt0
        .globl  _main

__start::
        di                      ; disable interrupts — stub runs without them
        ld      sp, #0x77FE     ; stack at top of usable data buffer area
        call    _main
__loop:
        halt
        jr      __loop

; crt0.s — Menu stub page layout for JLPiCart.
;
; The Menu Page is 16KB, mapped at Z80 page 1 (0x4000–0x7FFF, subslot 1).
; ROM header layout:
;   0x4000–0x403F  MenuStubHeader (64 bytes, "JLMN")
;   0x4040–0x407F  MenuMailboxRegs (64 bytes, zeroed)
;   0x4080–0x40FF  Reserved (128 bytes)
;   0x4100–0x7FFF  Shared data buffer and stub code
;
; The RP2350 calls stub_entry (page-relative 0x0100 → absolute 0x4100) to
; start the stub.  This is NOT a BIOS-scanned cartridge; the RP2350 explicitly
; maps this page and calls the entrypoint via the bus layer.
;
; Built with pinned SDCC 4.5.0 assembler (sdasz80).

        .module crt0
        .globl  _stub_main

        ; ---------------------------------------------------------------
        ; MenuStubHeader — 64 bytes at 0x4000
        ; ---------------------------------------------------------------
        .area   _HEADER (ABS)
        .org    0x4000

        ; sig[4] = "JLMN"
        .db     0x4A, 0x4C, 0x4D, 0x4E
        ; abi_major=1, abi_minor=0
        .db     0x01, 0x00
        ; header_len = 64
        .dw     0x0040
        ; mailbox_ofs = 0x0040
        .dw     0x0040
        ; data_ofs = 0x0100
        .dw     0x0100
        ; data_len = 0x3F00
        .dw     0x3F00
        ; stub_entry = 0x0100 (offset within page; absolute = 0x4100)
        .dw     0x0100
        ; host_caps = 0 (filled at runtime by _stub_main)
        .dw     0x0000, 0x0000
        ; vdp_caps = 0 (filled at runtime by _stub_main)
        .dw     0x0000, 0x0000
        ; build_id = 0x0001
        .dw     0x0001
        ; reserved0 = 0
        .dw     0x0000
        ; reserved[36]
        .ds     36

        ; ---------------------------------------------------------------
        ; MenuMailboxRegs — 64 bytes at 0x4040 (all zero initially)
        ; ---------------------------------------------------------------
        .ds     64

        ; ---------------------------------------------------------------
        ; Reserved — 128 bytes at 0x4080
        ; ---------------------------------------------------------------
        .ds     128

        ; ---------------------------------------------------------------
        ; Stub entry and code — start at 0x4100
        ; ---------------------------------------------------------------
        .org    0x4100

_stub_entry:
        ; Initialise stack in upper RAM (safe on all MSX generations).
        ld      sp, #0xF380
        call    _stub_main      ; call C main — should never return
_halt:
        jp      _halt

        .area   _CODE
        .area   _DATA
        .area   _BSS

;--------------------------------------------------------
; File Created by SDCC : free open source ISO C Compiler
; Version 4.5.0 #15242 (Linux)
;--------------------------------------------------------
	.module main
	
	.optsdcc -mz80 sdcccall(1)
;--------------------------------------------------------
; Public variables in this module
;--------------------------------------------------------
	.globl _stub_main
	.globl _bios_gttrig
	.globl _bios_gtstck
	.globl _bios_snsmat
	.globl _bios_cls
	.globl _bios_posit
	.globl _bios_chput
;--------------------------------------------------------
; special function registers
;--------------------------------------------------------
;--------------------------------------------------------
; ram data
;--------------------------------------------------------
	.area _DATA
;--------------------------------------------------------
; ram data
;--------------------------------------------------------
	.area _INITIALIZED
_g_mode:
	.ds 1
;--------------------------------------------------------
; absolute external ram data
;--------------------------------------------------------
	.area _DABS (ABS)
;--------------------------------------------------------
; global & static initialisations
;--------------------------------------------------------
	.area _HOME
	.area _GSINIT
	.area _GSFINAL
	.area _GSINIT
;--------------------------------------------------------
; Home
;--------------------------------------------------------
	.area _HOME
	.area _HOME
;--------------------------------------------------------
; code
;--------------------------------------------------------
	.area _CODE
;main.c:83: static uint16_t rd16(volatile const uint8_t *p)
;	---------------------------------
; Function rd16
; ---------------------------------
_rd16:
	push	ix
	ld	ix,#0
	add	ix,sp
;main.c:85: return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
	push	hl
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
;main.c:86: }
	ld	sp, ix
	pop	ix
	ret
;main.c:88: static void wr16(volatile uint8_t *p, uint16_t v)
;	---------------------------------
; Function wr16
; ---------------------------------
_wr16:
	push	ix
	ld	ix,#0
	add	ix,sp
;main.c:90: p[0] = (uint8_t)(v & 0xFFu);
	push	hl
	ld	(hl), e
;main.c:91: p[1] = (uint8_t)(v >> 8);
	inc	hl
	ld	(hl), d
;main.c:92: }
	ld	sp, ix
	pop	ix
	ret
;main.c:94: static uint32_t rd32(volatile const uint8_t *p)
;	---------------------------------
; Function rd32
; ---------------------------------
_rd32:
	push	ix
	ld	ix,#0
	add	ix,sp
	ld	iy, #-10
	add	iy, sp
	ld	sp, iy
;main.c:96: return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
	ld	-2 (ix), l
	ld	-1 (ix), h
	ld	c, l
	ld	b, h
	ld	a, (bc)
	ld	-10 (ix), a
	xor	a, a
	ld	-9 (ix), a
	ld	-8 (ix), a
	ld	-7 (ix), a
	ld	l, c
	ld	h, b
	inc	hl
	ld	e, (hl)
	ld	d, #0x00
	ld	hl, #0x0000
	ld	h, l
	ld	l, d
	ld	d, e
	ld	a, -10 (ix)
	ld	-6 (ix), a
	ld	-5 (ix), d
	ld	-4 (ix), l
	ld	-3 (ix), h
;main.c:97: | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
	ld	l, c
	ld	h, b
	inc	hl
	inc	hl
	ld	l, (hl)
	ld	h, #0x00
	ld	e, -6 (ix)
	ld	d, -5 (ix)
	push	hl
	pop	iy
	ld	hl, #3
	add	hl, bc
	ld	c, (hl)
	push	iy
	ld	l, -12 (ix)
	pop	iy
	ld	h, c
;main.c:98: }
	ld	sp, ix
	pop	ix
	ret
;main.c:100: static void wr32(volatile uint8_t *p, uint32_t v)
;	---------------------------------
; Function wr32
; ---------------------------------
_wr32:
;main.c:102: p[0] = (uint8_t)(v & 0xFFu);
	ld	c, l
	ld	b, h
	push	bc
	ld	iy, #4
	add	iy, sp
	ld	a, 0 (iy)
	ld	(bc), a
;main.c:103: p[1] = (uint8_t)((v >> 8)  & 0xFFu);
	ld	e, c
	ld	d, b
	inc	de
	ld	a, 1 (iy)
	ld	(de), a
;main.c:104: p[2] = (uint8_t)((v >> 16) & 0xFFu);
	ld	e, c
	ld	d, b
	inc	de
	inc	de
	ld	a, 2 (iy)
	ld	(de), a
;main.c:105: p[3] = (uint8_t)((v >> 24) & 0xFFu);
	inc	bc
	inc	bc
	inc	bc
	ld	a, 3 (iy)
	ld	(bc), a
;main.c:106: }
	pop	af
	pop	hl
	pop	af
	pop	af
	jp	(hl)
;main.c:113: void bios_chput(uint8_t c) __naked
;	---------------------------------
; Function bios_chput
; ---------------------------------
_bios_chput::
;main.c:120: __endasm;
	ld	a, l
	call	0x00A2
	ret
;main.c:121: }
;main.c:127: void bios_posit(uint8_t row, uint8_t col)
;	---------------------------------
; Function bios_posit
; ---------------------------------
_bios_posit::
;main.c:134: __endasm;
	ld	h, 4(ix) ; row (first arg, IX+4)
	ld	l, 6(ix) ; col (second arg, IX+6)
	call	0x00C6
;main.c:135: }
	ret
;main.c:138: void bios_cls(void) __naked
;	---------------------------------
; Function bios_cls
; ---------------------------------
_bios_cls::
;main.c:143: __endasm;
	call	0x009F
	ret
;main.c:144: }
;main.c:147: uint8_t bios_snsmat(uint8_t row) __naked
;	---------------------------------
; Function bios_snsmat
; ---------------------------------
_bios_snsmat::
;main.c:155: __endasm;
	ld	e, l ; row argument
	call	0x0141
	ld	l, a ; return value in L
	ret
;main.c:156: }
;main.c:160: uint8_t bios_gtstck(uint8_t stick) __naked
;	---------------------------------
; Function bios_gtstck
; ---------------------------------
_bios_gtstck::
;main.c:168: __endasm;
	ld	a, l
	call	0x00D5
	ld	l, a
	ret
;main.c:169: }
;main.c:173: uint8_t bios_gttrig(uint8_t trigger) __naked
;	---------------------------------
; Function bios_gttrig
; ---------------------------------
_bios_gttrig::
;main.c:181: __endasm;
	ld	a, l
	call	0x00D8
	ld	l, a
	ret
;main.c:182: }
;main.c:191: static uint8_t peek(uint16_t addr) __naked
;	---------------------------------
; Function peek
; ---------------------------------
_peek:
;main.c:197: __endasm;
	ld	l, (hl) ; addr is in HL; dereference → return byte in L
	ret
;main.c:198: }
;main.c:202: static uint8_t detect_msx_gen(void)
;	---------------------------------
; Function detect_msx_gen
; ---------------------------------
_detect_msx_gen:
;main.c:204: return (uint8_t)(peek(0x002Du) & 0x03u);
	ld	hl, #0x002d
	call	_peek
	and	a, #0x03
;main.c:205: }
	ret
;main.c:209: static uint8_t detect_vram_kb(void)
;	---------------------------------
; Function detect_vram_kb
; ---------------------------------
_detect_vram_kb:
;main.c:211: uint8_t gen = detect_msx_gen();
	call	_detect_msx_gen
;main.c:212: if (gen == 0) return 16u;  /* MSX1: always 16KB */
	or	a, a
	jr	NZ, 00102$
	ld	a, #0x10
	ret
00102$:
;main.c:213: return peek(0xF3AEu);
	ld	hl, #0xf3ae
;main.c:214: }
	jp	_peek
;main.c:217: static uint32_t build_host_caps(uint8_t gen)
;	---------------------------------
; Function build_host_caps
; ---------------------------------
_build_host_caps:
;main.c:219: uint32_t caps = 0;
	ld	de, #0x0000
;main.c:220: if (gen == 0) caps |= (1u << 0); /* MSX1 */
	or	a, a
	jr	NZ, 00102$
	ld	de, #0x0001
00102$:
;main.c:221: if (gen == 1) caps |= (1u << 1); /* MSX2 */
	cp	a, #0x01
	jr	NZ, 00104$
	set	1, e
00104$:
;main.c:222: if (gen == 2) caps |= (1u << 2); /* MSX2+ */
	cp	a, #0x02
	jr	NZ, 00106$
	set	2, e
00106$:
;main.c:223: if (gen == 3) caps |= (1u << 3); /* turboR */
	sub	a, #0x03
	jr	NZ, 00108$
	set	3, e
00108$:
;main.c:224: caps |= (1u << 5);               /* BIOS kbd always available on MSX */
	set	5, e
	ld	d, #0x00
	ld	hl, #0x0000
;main.c:225: return caps;
;main.c:226: }
	ret
;main.c:229: static uint32_t build_vdp_caps(uint8_t gen)
;	---------------------------------
; Function build_vdp_caps
; ---------------------------------
_build_vdp_caps:
;main.c:231: uint32_t caps = (1u << 0);       /* TMS9918-compatible VRAM port writes */
	ld	de, #0x0001
	ld	hl, #0x0000
;main.c:232: if (gen >= 2) caps |= (1u << 1); /* palette programmable (MSX2+) */
	sub	a, #0x02
	ret	C
	ld	de, #0x0003
;main.c:233: return caps;
;main.c:234: }
	ret
;main.c:246: static void cmd_get_host_info(volatile uint8_t *data, uint16_t data_cap)
;	---------------------------------
; Function cmd_get_host_info
; ---------------------------------
_cmd_get_host_info:
	push	ix
	ld	ix,#0
	add	ix,sp
	push	af
	dec	sp
	ld	-2 (ix), l
	ld	-1 (ix), h
;main.c:248: uint8_t gen     = detect_msx_gen();
	call	_detect_msx_gen
	ld	-3 (ix), a
;main.c:249: uint8_t vram_kb = detect_vram_kb();
	call	_detect_vram_kb
	ld	e, a
;main.c:252: data[0] = (uint8_t)(gen + 1u);  /* 1=MSX1, 2=MSX2, ... */
	ld	c, -2 (ix)
	ld	b, -1 (ix)
	ld	a, -3 (ix)
	inc	a
	ld	(bc), a
;main.c:253: data[1] = vram_kb;
	ld	l, c
	ld	h, b
	inc	hl
	ld	(hl), e
;main.c:254: data[2] = 40u;                  /* text_cols: 40 for MSX1 baseline */
	ld	l, c
	ld	h, b
	inc	hl
	inc	hl
	ld	(hl), #0x28
;main.c:255: data[3] = 0u;                   /* reserved0 */
	ld	l, c
	ld	h, b
	inc	hl
	inc	hl
	inc	hl
	ld	(hl), #0x00
;main.c:256: wr32(data + 4, build_host_caps(gen));
	push	bc
	ld	a, -3 (ix)
	call	_build_host_caps
	push	hl
	pop	iy
	pop	bc
	ld	hl, #0x0004
	add	hl, bc
	push	bc
	push	iy
	push	de
	call	_wr32
;main.c:257: wr32(data + 8, build_vdp_caps(gen));
	ld	a, -3 (ix)
	call	_build_vdp_caps
	push	hl
	pop	iy
	pop	bc
	ld	hl, #0x0008
	add	hl, bc
	push	iy
	push	de
	call	_wr32
;main.c:260: wr32(HEADER_BASE + HDR_HOST_CAPS, build_host_caps(gen));
	ld	a, -3 (ix)
	call	_build_host_caps
	push	hl
	push	de
	ld	hl, #0x4010
	call	_wr32
;main.c:261: wr32(HEADER_BASE + HDR_VDP_CAPS,  build_vdp_caps(gen));
	ld	a, -3 (ix)
	call	_build_vdp_caps
	push	hl
	push	de
	ld	hl, #0x4014
	call	_wr32
;main.c:263: wr16(MAILBOX_BASE + MBX_OUT_LEN, 12u); /* sizeof(HostInfo) */
	ld	de, #0x000c
	ld	hl, #0x405a
	call	_wr16
;main.c:264: wr16(MAILBOX_BASE + MBX_STATUS,  MENU_OK);
	ld	de, #0x0000
	ld	hl, #0x4046
	call	_wr16
;main.c:266: (void)data_cap;
;main.c:267: }
	ld	sp, ix
	pop	ix
	ret
;main.c:269: static void cmd_set_mode(uint8_t mode_id)
;	---------------------------------
; Function cmd_set_mode
; ---------------------------------
_cmd_set_mode:
;main.c:271: if (mode_id == MODE_TEXT_40) {
	or	a, a
	jr	NZ, 00102$
;main.c:275: __endasm;
	call	0x006C ; INITXT — initialise 40-col text mode
;main.c:276: g_mode = MODE_TEXT_40;
	xor	a, a
	ld	(_g_mode+0), a
;main.c:277: wr16(MAILBOX_BASE + MBX_STATUS, MENU_OK);
	ld	de, #0x0000
	ld	hl, #0x4046
	jp	_wr16
00102$:
;main.c:279: wr16(MAILBOX_BASE + MBX_STATUS, MENU_E_UNSUPPORTED);
	ld	de, #0x0001
	ld	hl, #0x4046
;main.c:281: }
	jp	_wr16
;main.c:283: static void cmd_clear(void)
;	---------------------------------
; Function cmd_clear
; ---------------------------------
_cmd_clear:
;main.c:285: if (g_mode != MODE_TEXT_40) {
	ld	a, (_g_mode+0)
	or	a, a
	jr	Z, 00102$
;main.c:286: wr16(MAILBOX_BASE + MBX_STATUS, MENU_E_BAD_STATE);
	ld	de, #0x0003
;main.c:287: return;
	ld	hl, #0x4046
	jp	_wr16
00102$:
;main.c:289: bios_cls();
	call	_bios_cls
;main.c:290: wr16(MAILBOX_BASE + MBX_STATUS, MENU_OK);
	ld	de, #0x0000
	ld	hl, #0x4046
;main.c:291: }
	jp	_wr16
;main.c:293: static void cmd_put_text(uint32_t arg0, uint32_t arg2, uint16_t in_len,
;	---------------------------------
; Function cmd_put_text
; ---------------------------------
_cmd_put_text:
	push	ix
	ld	ix,#0
	add	ix,sp
;main.c:296: uint8_t  x    = (uint8_t)(arg0 & 0xFFu);
	ld	c, e
;main.c:297: uint8_t  y    = (uint8_t)((arg0 >> 8) & 0xFFu);
;main.c:298: uint8_t  wrap = (uint8_t)(arg2 & 0x01u); /* arg2 bit 0 = wrap enable */
	ld	a, 4 (ix)
	and	a, #0x01
	ld	e, a
;main.c:301: if (g_mode != MODE_TEXT_40) {
	ld	a, (_g_mode+0)
	or	a, a
	jr	Z, 00102$
;main.c:302: wr16(MAILBOX_BASE + MBX_STATUS, MENU_E_BAD_STATE);
	ld	de, #0x0003
	ld	hl, #0x4046
	call	_wr16
;main.c:303: return;
	jr	00114$
00102$:
;main.c:307: if (!wrap) {
	ld	a, e
	or	a, a
	jr	NZ, 00106$
;main.c:308: max_out = (x < 40u) ? (uint16_t)(40u - x) : 0u;
	ld	a, c
	sub	a, #0x28
	jr	NC, 00116$
	ld	e, c
	ld	a, #0x28
	sub	a, e
	jr	00117$
00116$:
	xor	a, a
00117$:
	ld	e, a
	ld	b, #0x00
;main.c:309: if (in_len > max_out) in_len = (uint16_t)max_out;
	ld	a, e
	sub	a, 8 (ix)
	ld	a, b
	sbc	a, 9 (ix)
	jr	NC, 00106$
	ld	8 (ix), e
	ld	9 (ix), b
00106$:
;main.c:313: bios_posit((uint8_t)(y + 1u), (uint8_t)(x + 1u));
	inc	c
	ld	a, d
	inc	a
	ld	l, c
	call	_bios_posit
;main.c:315: for (i = 0u; i < in_len; i++) {
	ld	bc, #0x0000
00112$:
	ld	a, c
	sub	a, 8 (ix)
	ld	a, b
	sbc	a, 9 (ix)
	jr	NC, 00110$
;main.c:316: uint8_t ch = data[i];
	ld	l, 10 (ix)
	ld	h, 11 (ix)
	add	hl, bc
	ld	a, (hl)
;main.c:318: if (ch < 0x20u || ch > 0x7Eu) ch = 0x3Fu; /* '?' */
	cp	a, #0x20
	jr	C, 00107$
	cp	a, #0x7f
	jr	C, 00108$
00107$:
	ld	a, #0x3f
00108$:
;main.c:319: bios_chput(ch);
	push	bc
	call	_bios_chput
	pop	bc
;main.c:315: for (i = 0u; i < in_len; i++) {
	inc	bc
	jr	00112$
00110$:
;main.c:322: wr16(MAILBOX_BASE + MBX_STATUS, MENU_OK);
	ld	de, #0x0000
	ld	hl, #0x4046
	call	_wr16
00114$:
;main.c:323: }
	pop	ix
	pop	hl
	pop	af
	pop	af
	pop	af
	pop	af
	jp	(hl)
;main.c:325: static void cmd_read_input(volatile uint8_t *data, uint16_t data_cap)
;	---------------------------------
; Function cmd_read_input
; ---------------------------------
_cmd_read_input:
	push	ix
	ld	ix,#0
	add	ix,sp
	push	af
	push	af
	push	af
	ld	-2 (ix), l
	ld	-1 (ix), h
;main.c:332: if (data_cap < 16u) {
	ld	a, e
	sub	a, #0x10
	ld	a, d
	sbc	a, #0x00
	jr	NC, 00149$
;main.c:333: wr16(MAILBOX_BASE + MBX_STATUS, MENU_E_OVERFLOW);
	ld	de, #0x0004
	ld	hl, #0x4046
	call	_wr16
;main.c:334: return;
	jp	00146$
;main.c:338: for (i = 0u; i < 11u; i++) {
00149$:
	ld	e, #0x00
00144$:
;main.c:339: data[i] = bios_snsmat(i);
	ld	l, -2 (ix)
	ld	h, -1 (ix)
	ld	d, #0x00
	add	hl, de
	push	hl
	push	de
	ld	a, e
	call	_bios_snsmat
	pop	de
	pop	hl
	ld	(hl), a
;main.c:338: for (i = 0u; i < 11u; i++) {
	inc	e
	ld	a, e
	sub	a, #0x0b
	jr	C, 00144$
;main.c:345: joy1_dir = bios_gtstck(1u);
	ld	a, #0x01
	call	_bios_gtstck
	ld	c, a
;main.c:346: trig_a1  = bios_gttrig(0u); /* 0xFF=pressed, 0x00=released */
	push	bc
	xor	a, a
	call	_bios_gttrig
	ld	-5 (ix), a
;main.c:347: trig_b1  = bios_gttrig(1u);
	ld	a, #0x01
	call	_bios_gttrig
	ld	-4 (ix), a
	pop	bc
;main.c:348: joy1 = 0xFFu;
	ld	e, #0xff
;main.c:350: if (joy1_dir == 1u || joy1_dir == 2u || joy1_dir == 8u) joy1 &= ~(1u << 0); /* Up */
	ld	a, c
	sub	a, #0x02
	ld	a, #0x01
	jr	Z, 00311$
	xor	a, a
00311$:
	ld	-3 (ix), a
	ld	a, c
	sub	a, #0x08
	ld	a, #0x01
	jr	Z, 00313$
	xor	a, a
00313$:
	ld	b, a
	ld	a, c
	dec	a
	jr	Z, 00104$
	ld	a, -3 (ix)
	or	a, a
	jr	NZ, 00104$
	or	a, b
	jr	Z, 00105$
00104$:
	ld	e, #0xfe
00105$:
;main.c:351: if (joy1_dir == 4u || joy1_dir == 5u || joy1_dir == 6u) joy1 &= ~(1u << 1); /* Down */
	ld	a, c
	sub	a, #0x04
	ld	a, #0x01
	jr	Z, 00316$
	xor	a, a
00316$:
	ld	l, a
	ld	a, c
	sub	a, #0x06
	ld	a, #0x01
	jr	Z, 00318$
	xor	a, a
00318$:
	ld	d, a
	ld	a, l
	or	a, a
	jr	NZ, 00108$
	ld	a, c
	sub	a, #0x05
	jr	Z, 00108$
	ld	a, d
	or	a, a
	jr	Z, 00109$
00108$:
	res	1, e
00109$:
;main.c:352: if (joy1_dir == 6u || joy1_dir == 7u || joy1_dir == 8u) joy1 &= ~(1u << 2); /* Left */
	ld	a, d
	or	a, a
	jr	NZ, 00112$
	ld	a, c
	sub	a, #0x07
	jr	Z, 00112$
	ld	a, b
	or	a, a
	jr	Z, 00113$
00112$:
	res	2, e
00113$:
;main.c:353: if (joy1_dir == 2u || joy1_dir == 3u || joy1_dir == 4u) joy1 &= ~(1u << 3); /* Right */
	ld	a, -3 (ix)
	or	a, a
	jr	NZ, 00116$
	ld	a, c
	sub	a, #0x03
	jr	Z, 00116$
	ld	a, l
	or	a, a
	jr	Z, 00117$
00116$:
	res	3, e
00117$:
;main.c:354: if (trig_a1 != 0u) joy1 &= ~(1u << 4); /* 0xFF=pressed → clear bit */
	ld	a, -5 (ix)
	or	a, a
	jr	Z, 00121$
	res	4, e
00121$:
;main.c:355: if (trig_b1 != 0u) joy1 &= ~(1u << 5);
	ld	a, -4 (ix)
	or	a, a
	jr	Z, 00123$
	res	5, e
00123$:
;main.c:358: joy2_dir = bios_gtstck(2u);
	push	de
	ld	a, #0x02
	call	_bios_gtstck
	pop	de
	ld	d, a
;main.c:359: trig_a2  = bios_gttrig(2u);
	push	de
	ld	a, #0x02
	call	_bios_gttrig
	ld	-6 (ix), a
;main.c:360: trig_b2  = bios_gttrig(3u);
	ld	a, #0x03
	call	_bios_gttrig
	ld	l, a
	pop	de
;main.c:361: joy2 = 0xFFu;
	ld	c, #0xff
;main.c:362: if (joy2_dir == 1u || joy2_dir == 2u || joy2_dir == 8u) joy2 &= ~(1u << 0);
	ld	a, d
	sub	a, #0x02
	ld	a, #0x01
	jr	Z, 00323$
	xor	a, a
00323$:
	ld	-5 (ix), a
	ld	a, d
	sub	a, #0x08
	ld	a, #0x01
	jr	Z, 00325$
	xor	a, a
00325$:
	ld	-4 (ix), a
	ld	a, d
	dec	a
	jr	Z, 00124$
	ld	a, -5 (ix)
	or	a, a
	jr	NZ, 00124$
	ld	a, -4 (ix)
	or	a, a
	jr	Z, 00125$
00124$:
	ld	c, #0xfe
00125$:
;main.c:363: if (joy2_dir == 4u || joy2_dir == 5u || joy2_dir == 6u) joy2 &= ~(1u << 1);
	ld	a, d
	sub	a, #0x04
	ld	a, #0x01
	jr	Z, 00328$
	xor	a, a
00328$:
	ld	-3 (ix), a
	ld	a, d
	sub	a, #0x06
	ld	a, #0x01
	jr	Z, 00330$
	xor	a, a
00330$:
	ld	b, a
	ld	a, -3 (ix)
	or	a, a
	jr	NZ, 00128$
	ld	a, d
	sub	a, #0x05
	jr	Z, 00128$
	ld	a, b
	or	a, a
	jr	Z, 00129$
00128$:
	res	1, c
00129$:
;main.c:364: if (joy2_dir == 6u || joy2_dir == 7u || joy2_dir == 8u) joy2 &= ~(1u << 2);
	ld	a, b
	or	a, a
	jr	NZ, 00132$
	ld	a, d
	sub	a, #0x07
	jr	Z, 00132$
	ld	a, -4 (ix)
	or	a, a
	jr	Z, 00133$
00132$:
	res	2, c
00133$:
;main.c:365: if (joy2_dir == 2u || joy2_dir == 3u || joy2_dir == 4u) joy2 &= ~(1u << 3);
	ld	a, -5 (ix)
	or	a, a
	jr	NZ, 00136$
	ld	a, d
	sub	a, #0x03
	jr	Z, 00136$
	ld	a, -3 (ix)
	or	a, a
	jr	Z, 00137$
00136$:
	res	3, c
00137$:
;main.c:366: if (trig_a2 != 0u) joy2 &= ~(1u << 4);
	ld	a, -6 (ix)
	or	a, a
	jr	Z, 00141$
	res	4, c
00141$:
;main.c:367: if (trig_b2 != 0u) joy2 &= ~(1u << 5);
	ld	a, l
	or	a, a
	jr	Z, 00143$
	res	5, c
00143$:
;main.c:369: data[11] = joy1;
	ld	a, -2 (ix)
	add	a, #0x0b
	ld	l, a
	ld	a, -1 (ix)
	adc	a, #0x00
	ld	h, a
	ld	(hl), e
;main.c:370: data[12] = joy2;
	ld	l, -2 (ix)
	ld	h, -1 (ix)
	ld	de, #0x000c
	add	hl, de
	ld	(hl), c
;main.c:371: data[13] = 0u;
	ld	l, -2 (ix)
	ld	h, -1 (ix)
	ld	de, #0x000d
	add	hl, de
	ld	(hl), #0x00
;main.c:372: data[14] = 0u;
	ld	l, -2 (ix)
	ld	h, -1 (ix)
	ld	de, #0x000e
	add	hl, de
	ld	(hl), #0x00
;main.c:373: data[15] = 0u;
	ld	l, -2 (ix)
	ld	h, -1 (ix)
	ld	de, #0x000f
	add	hl, de
;main.c:375: wr16(MAILBOX_BASE + MBX_OUT_LEN, 16u); /* sizeof(InputSnapshot) */
	ld	de, #0x0010
	ld	(hl), d
	ld	hl, #0x405a
	call	_wr16
;main.c:376: wr16(MAILBOX_BASE + MBX_STATUS,  MENU_OK);
	ld	de, #0x0000
	ld	hl, #0x4046
	call	_wr16
00146$:
;main.c:377: }
	ld	sp, ix
	pop	ix
	ret
;main.c:379: static void cmd_idle(uint32_t arg0)
;	---------------------------------
; Function cmd_idle
; ---------------------------------
_cmd_idle:
;main.c:384: uint16_t ms = (uint16_t)(arg0 & 0xFFFFu);
;main.c:386: for (i = 0u; i < ms; i++) {
	ld	bc, #0x0000
00107$:
	ld	a, c
	sub	a, e
	ld	a, b
	sbc	a, d
	jr	NC, 00102$
;main.c:387: for (j = 0u; j < 875u; j++) {
	ld	hl, #0x036b
00105$:
;main.c:391: __endasm;
	nop
	dec	hl
;main.c:387: for (j = 0u; j < 875u; j++) {
	ld	a, h
	or	a, l
	jr	NZ, 00105$
;main.c:386: for (i = 0u; i < ms; i++) {
	inc	bc
	jr	00107$
00102$:
;main.c:394: wr16(MAILBOX_BASE + MBX_STATUS, MENU_OK);
	ld	de, #0x0000
	ld	hl, #0x4046
;main.c:395: }
	jp	_wr16
;main.c:401: void stub_main(void)
;	---------------------------------
; Function stub_main
; ---------------------------------
_stub_main::
	push	ix
	ld	ix,#0
	add	ix,sp
	ld	hl, #-6
	add	hl, sp
	ld	sp, hl
;main.c:403: uint8_t gen = detect_msx_gen();
	call	_detect_msx_gen
	ld	c, a
;main.c:416: __endasm;
	call	0x006C ; INITXT — 40-col text mode
;main.c:417: g_mode = MODE_TEXT_40;
	xor	a, a
	ld	(_g_mode+0), a
;main.c:419: wr32(HEADER_BASE + HDR_HOST_CAPS, build_host_caps(gen));
	push	bc
	ld	a, c
	call	_build_host_caps
	push	hl
	push	de
	ld	hl, #0x4010
	call	_wr32
	pop	bc
;main.c:420: wr32(HEADER_BASE + HDR_VDP_CAPS,  build_vdp_caps(gen));
	ld	a, c
	call	_build_vdp_caps
	push	hl
	push	de
	ld	hl, #0x4014
	call	_wr32
;main.c:423: while (1) {
00116$:
;main.c:424: uint16_t cmd_seq  = rd16(MAILBOX_BASE + MBX_CMD_SEQ);
	ld	hl, #0x4040
	call	_rd16
;main.c:425: uint16_t resp_seq = rd16(MAILBOX_BASE + MBX_RESP_SEQ);
	push	de
	ld	hl, #0x4042
	call	_rd16
	ex	de, hl
	pop	de
;main.c:427: if (cmd_seq == resp_seq) {
	cp	a, a
	sbc	hl, de
	jr	Z, 00116$
;main.c:432: uint16_t cmd_id = rd16(MAILBOX_BASE + MBX_CMD_ID);
	push	de
	ld	hl, #0x4044
	call	_rd16
	ld	c, e
	ld	b, d
	pop	de
;main.c:433: uint32_t arg0   = rd32(MAILBOX_BASE + MBX_ARG0);
	push	bc
	push	de
	ld	hl, #0x4048
	call	_rd32
	ld	-6 (ix), e
	ld	-5 (ix), d
	ld	-4 (ix), l
	ld	-3 (ix), h
;main.c:434: uint16_t in_len = rd16(MAILBOX_BASE + MBX_IN_LEN);
	ld	hl, #0x4058
	call	_rd16
	ld	-2 (ix), e
	ld	-1 (ix), d
	pop	de
	pop	bc
;main.c:437: if (in_len > DATA_LEN) {
	ld	l, -2 (ix)
	ld	h, -1 (ix)
	xor	a, a
	cp	a, l
	ld	a, #0x3f
	sbc	a, h
	jr	NC, 00104$
;main.c:438: wr16(MAILBOX_BASE + MBX_STATUS, MENU_E_OVERFLOW);
	push	de
	ld	de, #0x0004
	ld	hl, #0x4046
	call	_wr16
	pop	de
;main.c:439: wr16(MAILBOX_BASE + MBX_RESP_SEQ, cmd_seq);
	ld	hl, #0x4042
	call	_wr16
;main.c:440: continue;
	jr	00116$
00104$:
;main.c:444: switch (cmd_id) {
	ld	a, #0x0b
	cp	a, c
	ld	a, #0x00
	sbc	a, b
	jp	C, 00113$
	ld	hl, #00149$
	add	hl, bc
	add	hl, bc
	ld	c, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, c
	jp	(hl)
00149$:
	.dw	00105$
	.dw	00106$
	.dw	00107$
	.dw	00108$
	.dw	00109$
	.dw	00110$
	.dw	00113$
	.dw	00113$
	.dw	00113$
	.dw	00113$
	.dw	00111$
	.dw	00112$
;main.c:445: case CMD_NOP:
00105$:
;main.c:446: wr16(MAILBOX_BASE + MBX_STATUS, MENU_OK);
	push	de
	ld	de, #0x0000
	ld	hl, #0x4046
	call	_wr16
	pop	de
;main.c:447: break;
	jp	00114$
;main.c:449: case CMD_GET_HOST_INFO:
00106$:
;main.c:450: cmd_get_host_info(DATA_BASE, DATA_LEN);
	push	de
	ld	de, #0x3f00
	ld	hl, #0x4100
	call	_cmd_get_host_info
	pop	de
;main.c:451: break;
	jr	00114$
;main.c:453: case CMD_SET_MODE:
00107$:
;main.c:454: cmd_set_mode((uint8_t)(arg0 & 0xFFu));
	ld	a, -6 (ix)
	push	de
	call	_cmd_set_mode
	pop	de
;main.c:455: break;
	jr	00114$
;main.c:457: case CMD_CLEAR:
00108$:
;main.c:458: cmd_clear();
	push	de
	call	_cmd_clear
	pop	de
;main.c:459: break;
	jr	00114$
;main.c:461: case CMD_PUT_TEXT: {
00109$:
;main.c:462: uint32_t arg2 = rd32(MAILBOX_BASE + MBX_ARG2);
	push	de
	ld	hl, #0x4050
	call	_rd32
	push	de
	pop	iy
;main.c:463: cmd_put_text(arg0, arg2, in_len, DATA_BASE);
	ld	bc, #0x4100
	push	bc
	ld	c, -2 (ix)
	ld	b, -1 (ix)
	push	bc
	push	hl
	push	iy
	ld	e, -6 (ix)
	ld	d, -5 (ix)
	ld	l, -4 (ix)
	ld	h, -3 (ix)
	call	_cmd_put_text
	pop	de
;main.c:464: break;
	jr	00114$
;main.c:467: case CMD_READ_INPUT:
00110$:
;main.c:468: cmd_read_input(DATA_BASE, DATA_LEN);
	push	de
	ld	de, #0x3f00
	ld	hl, #0x4100
	call	_cmd_read_input
	pop	de
;main.c:469: break;
	jr	00114$
;main.c:471: case CMD_IDLE:
00111$:
;main.c:472: cmd_idle(arg0);
	ex	de,hl
	pop	de
	push	de
	push	hl
	ld	l, -4 (ix)
	ld	h, -3 (ix)
	call	_cmd_idle
	pop	de
;main.c:473: break;
	jr	00114$
;main.c:475: case CMD_LAUNCH:
00112$:
;main.c:481: wr16(MAILBOX_BASE + MBX_STATUS,   MENU_OK);
	push	de
	ld	de, #0x0000
	ld	hl, #0x4046
	call	_wr16
	pop	de
;main.c:482: wr16(MAILBOX_BASE + MBX_RESP_SEQ, cmd_seq);
	push	de
	ld	hl, #0x4042
	call	_wr16
	pop	de
;main.c:485: __endasm;
	jp	0x0000
;main.c:486: break; /* unreachable; suppresses SDCC fallthrough warning */
	jr	00114$
;main.c:488: default:
00113$:
;main.c:489: wr16(MAILBOX_BASE + MBX_STATUS, MENU_E_UNSUPPORTED);
	push	de
	ld	de, #0x0001
	ld	hl, #0x4046
	call	_wr16
	pop	de
;main.c:491: }
00114$:
;main.c:494: wr16(MAILBOX_BASE + MBX_RESP_SEQ, cmd_seq);
	ld	hl, #0x4042
	call	_wr16
;main.c:496: }
	jp	00116$
	.area _CODE
	.area _INITIALIZER
__xinit__g_mode:
	.db #0x00	; 0
	.area _CABS (ABS)

; z80dasm 1.1.6
; command line: z80dasm -l -t -a -g 0x4000 -o da.asm z80stub.rom

	org	04000h

	ld b,c			;4000	41 	A 
	ld b,d			;4001	42 	B 
l4002h:
	jr $+66		;4002	18 40 	. @ 
	nop			;4004	00 	. 
	nop			;4005	00 	. 
	nop			;4006	00 	. 
	nop			;4007	00 	. 
	nop			;4008	00 	. 
	nop			;4009	00 	. 
	nop			;400a	00 	. 
	nop			;400b	00 	. 
	nop			;400c	00 	. 
	nop			;400d	00 	. 
	nop			;400e	00 	. 
	nop			;400f	00 	. 
l4010h:
	ld hl,(l4002h)		;4010	2a 02 40 	* . @ 
	ld a,h			;4013	7c 	| 
	or a			;4014	b7 	. 
	jr z,l4010h		;4015	28 f9 	( . 
	jp (hl)			;4017	e9 	. 
	di			;4018	f3 	. 
	ld de,0e000h		;4019	11 00 e0 	. . . 
	ld hl,l4010h		;401c	21 10 40 	! . @ 
	ld bc,00008h		;401f	01 08 00 	. . . 
	ldir		;4022	ed b0 	. . 
	ld de,07fffh		;4024	11 ff 7f 	. .  
	ld hl,l402ah		;4027	21 2a 40 	! * @ 
l402ah:
	jp (hl)			;402a	e9 	. 
	rst 38h			;402b	ff 	. 
	rst 38h			;402c	ff 	. 
	rst 38h			;402d	ff 	. 
	rst 38h			;402e	ff 	. 
	rst 38h			;402f	ff 	. 

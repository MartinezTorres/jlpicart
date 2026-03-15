/* stub_bios.h — MSX BIOS entry-point constants for the Z80 menu stub.
 *
 * All addresses are page-0 BIOS entry points (slot 0, always visible from
 * the Z80 when executing at page 1 in a cartridge subslot).
 *
 * References: MSX2 Technical Handbook, BIOS entry point table.
 */

#ifndef STUB_BIOS_H
#define STUB_BIOS_H

/* Screen mode */
#define BIOS_CHGMOD  0x005F  /* A = mode_id (0=SCREEN0, 1=SCREEN1, …) */

/* Cursor and text output */
#define BIOS_POSIT   0x00C6  /* H = row (1-based), L = col (1-based) */
#define BIOS_CHPUT   0x00A2  /* A = character code; outputs to screen */

/* Keyboard matrix scan */
#define BIOS_SNSMAT  0x0141  /* A = row (0–10); returns A = row data (0=pressed) */

/* VDP ports (TMS9918/V9938 compatible) */
#define VDP_DATA     0x98    /* data read/write port */
#define VDP_CMD      0x99    /* command / register select port */

/* PSG ports (AY-3-8910) */
#define PSG_REG      0xA0    /* register select: OUT(PSG_REG, reg_num) */
#define PSG_WRITE    0xA1    /* data write:      OUT(PSG_WRITE, value) */
#define PSG_READ     0xA2    /* data read:       IN A,(PSG_READ) */

/* PSG register numbers */
#define PSG_R14_JOY  14      /* joystick port 1 direction + trigger bits */
#define PSG_R15_CTRL 15      /* joystick port select / port B control */

/* BIOS ROM version byte — used to detect MSX generation.
 * Address 0x002D: 0=MSX1, 1=MSX2, 2=MSX2+, 3=turboR (convention). */
#define BIOS_MSXVER  0x002D

#endif /* STUB_BIOS_H */

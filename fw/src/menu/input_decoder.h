#pragma once
// input_decoder.h — MSX keyboard matrix and joystick active-low decode helpers.
//
// All matrix bits are active-low: 0 = key pressed, 1 = released.
// Row indices and bit masks match the MSX BIOS SNSMAT convention used in stub.c.
//
// Row 7: bit7=RETURN  bit2=ESC
// Row 8: bit7=RIGHT   bit6=DOWN  bit5=UP  bit4=LEFT
//
// Joystick port 1 (PSG R14, bits 0-5, active-low):
//   bit0=Up  bit1=Down  bit2=Left  bit3=Right  bit4=TrigA

#include "msx/menu/menu_host_abi.h"  // InputSnapshot

// ---------------------------------------------------------------------------
// Generic bit test
// ---------------------------------------------------------------------------

inline bool key_pressed(const InputSnapshot& s, uint8_t row, uint8_t bit) {
    return (s.kbd_rows[row] & bit) == 0u;
}

// ---------------------------------------------------------------------------
// Named key helpers
// ---------------------------------------------------------------------------

inline bool key_return(const InputSnapshot& s) { return key_pressed(s, 7u, 0x80u); }
inline bool key_esc   (const InputSnapshot& s) { return key_pressed(s, 7u, 0x04u); }
inline bool key_up    (const InputSnapshot& s) { return key_pressed(s, 8u, 0x20u); }
inline bool key_down  (const InputSnapshot& s) { return key_pressed(s, 8u, 0x40u); }
inline bool key_left  (const InputSnapshot& s) { return key_pressed(s, 8u, 0x10u); }
inline bool key_right (const InputSnapshot& s) { return key_pressed(s, 8u, 0x80u); }

// ---------------------------------------------------------------------------
// Joystick port 1 (active-low)
// ---------------------------------------------------------------------------

inline bool joy1_up   (const InputSnapshot& s) { return (s.joy1 & 0x01u) == 0u; }
inline bool joy1_down (const InputSnapshot& s) { return (s.joy1 & 0x02u) == 0u; }
inline bool joy1_left (const InputSnapshot& s) { return (s.joy1 & 0x04u) == 0u; }
inline bool joy1_right(const InputSnapshot& s) { return (s.joy1 & 0x08u) == 0u; }
inline bool joy1_trig (const InputSnapshot& s) { return (s.joy1 & 0x10u) == 0u; }

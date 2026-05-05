#pragma once
// nextor_bin.h — Nextor ROM image placeholder.
//
// Nextor is the MSX-DOS2 derivative that supports mass-storage devices
// including the Sunrise ATA-IDE interface.  The ROM image is not bundled
// in this repository.
//
// To use Sunrise IDE emulation, obtain a Nextor kernel ROM built with
// the built-in Sunrise IDE driver and place it here as a byte array, or
// load it at runtime from the CONTENT_DATA flash region and pass the
// pointer to ide_setup().
//
// Download: https://github.com/Konamiman/Nextor
//   Build target: SUNRISE (produces Nextor2.1.x-SUNRISE.ROM, ~512 KB)
//
// Suggested runtime approach (avoids embedding a binary in source):
//   1. Install the Nextor ROM as a special collection entry in flash.
//   2. During boot, resolve its XIP pointer and pass to ide_setup().

// If NEXTOR_ROM_DATA is not defined by the build system or runtime path,
// sunrise_ide.cc falls back to nullptr (IDE hardware is emulated but the
// slot returns 0xFF — the Z80 will not boot Nextor until a real ROM is provided).
#ifndef NEXTOR_ROM_DATA
#  define NEXTOR_ROM_DATA  nullptr
#  define NEXTOR_ROM_SIZE  0u
#endif

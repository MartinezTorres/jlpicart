#!/usr/bin/env python3
"""
populate_flash.py — Generate a UF2 patch to pre-populate JLPiCart flash
with a single ROM collection (no USB host required).

Writes three KV records into the SYSTEM_KV region and the ROM bytes into
the CONTENT_DATA region, producing a UF2 that can be loaded with picotool.

Usage:
    python3 populate_flash.py <rom_file> [<output.uf2>] [options]

Example:
    python3 populate_flash.py 'Menace f Triton.MAPPER_LINEAR.rom' game.uf2
    picotool load game.uf2

Supported mapper names: rom, rom_32k_mirrored, konami, konami_z, ascii8, ascii16, ram
"""

import sys
import os
import re
import struct
import zlib
import argparse

# ---------------------------------------------------------------------------
# Flash layout (must match src/storage/flash_layout.h)
# ---------------------------------------------------------------------------

XIP_BASE               = 0x10000000
FLASH_SYSTEM_KV_OFS    = 0x200000
FLASH_CONTENT_DATA_OFS = 0x600000

# ---------------------------------------------------------------------------
# UF2 constants (https://github.com/microsoft/uf2)
# ---------------------------------------------------------------------------

UF2_MAGIC0        = 0x0A324655  # "UF2\n"
UF2_MAGIC1        = 0x9E5D5157
UF2_MAGIC_END     = 0x0AB16F30
UF2_FLAG_FAMILYID = 0x00002000
RP2350_FAMILY_ID  = 0x55114460  # RP2350-ARM-S
UF2_PAYLOAD_SIZE  = 256         # bytes of flash data per block

# ---------------------------------------------------------------------------
# KV store constants (must match src/storage/kv_store.h and collection_format.h)
# ---------------------------------------------------------------------------

KV_TYPE_LIVE      = 0x01
KV_COL_STATE      = "col.state"
KV_COL_RECORD     = "col.record"
KV_PAYLOAD_PREFIX = "pl."
KV_MAX_KEY_LEN    = 48

# Payload ID may be at most 45 chars so the "pl.<id>" key fits in 48 bytes.
PAYLOAD_ID_MAX    = 45

# ---------------------------------------------------------------------------
# Mapper name → canonical string mapping (handles MAPPER_LINEAR convention)
# ---------------------------------------------------------------------------

SUPPORTED_MAPPERS = [
    "rom",
    "rom_32k_mirrored",
    "konami",
    "konami_z",
    "ascii8",
    "ascii16",
    "ram",
]

# Filename-suffix aliases: the old testbed used MAPPER_LINEAR for linear/ROM.
_MAPPER_ALIASES = {
    "linear":           "rom",
    "rom":              "rom",
    "rom_32k_mirrored": "rom_32k_mirrored",
    "32k":              "rom_32k_mirrored",
    "konami":           "konami",
    "konami_z":         "konami_z",
    "ascii8":           "ascii8",
    "ascii16":          "ascii16",
    "ram":              "ram",
}

# ---------------------------------------------------------------------------
# CRC32 — IEEE 802.3 reflected, matches crc32.h / zlib.crc32()
# ---------------------------------------------------------------------------

def _kv_crc(type_byte: int, key: bytes, val: bytes) -> int:
    """CRC32 over {type, key_len, val_len_le16, key[], val[]}."""
    data = struct.pack("<BBH", type_byte, len(key), len(val)) + key + val
    return zlib.crc32(data) & 0xFFFFFFFF

# ---------------------------------------------------------------------------
# KV record serialisation
# ---------------------------------------------------------------------------

def make_kv_record(key: str, val: bytes) -> bytes:
    """Serialise one KV_TYPE_LIVE record (header + key + value)."""
    k = key.encode("ascii")
    assert 1 <= len(k) <= KV_MAX_KEY_LEN, f"KV key too long: {key!r}"
    assert len(val) <= 512, "KV value exceeds KV_MAX_VAL_LEN"
    crc = _kv_crc(KV_TYPE_LIVE, k, val)
    hdr = struct.pack("<BBHI", KV_TYPE_LIVE, len(k), len(val), crc)
    return hdr + k + val

# ---------------------------------------------------------------------------
# Struct serialisation (must match C packed structs in collection_format.h)
# ---------------------------------------------------------------------------

def _fixed_str(s: str, size: int) -> bytes:
    b = s.encode("ascii")[:size]
    return b + b"\x00" * (size - len(b))


def make_collection_record(collection_id: str,
                            version: str,
                            publisher_id: str,
                            title: str,
                            default_payload_id: str,
                            payload_count: int,
                            boot_mode: int = 1) -> bytes:
    """
    Serialise a CollectionRecord (357 bytes packed).

    Layout (matches struct CollectionRecord in collection_format.h):
      collection_id[64] + version[32] + publisher_id[64] + title[128]
      + boot_mode(1) + default_payload_id[64] + payload_count(1) + _pad[3]
      = 64+32+64+128+1+64+1+3 = 357 bytes
    """
    rec = (
        _fixed_str(collection_id,      64) +
        _fixed_str(version,            32) +
        _fixed_str(publisher_id,       64) +
        _fixed_str(title,              128) +
        struct.pack("B", boot_mode) +
        _fixed_str(default_payload_id, 64) +
        struct.pack("B", payload_count) +
        b"\x00" * 3   # _pad
    )
    assert len(rec) == 357, f"CollectionRecord size mismatch: {len(rec)}"
    return rec


def make_payload_record(payload_id: str,
                         mapper_type: str,
                         subslot: int,
                         data_flash_offset: int,
                         data_size: int) -> bytes:
    """
    Serialise a PayloadRecord (100 bytes packed).

    Layout (matches struct PayloadRecord in collection_format.h):
      payload_id[64] + mapper_type[24] + subslot(1) + _pad[3]
      + data_flash_offset(4) + data_size(4)
      = 64+24+1+3+4+4 = 100 bytes
    """
    rec = (
        _fixed_str(payload_id,  64) +
        _fixed_str(mapper_type, 24) +
        struct.pack("B", subslot) +
        b"\x00" * 3 +  # _pad
        struct.pack("<II", data_flash_offset, data_size)
    )
    assert len(rec) == 100, f"PayloadRecord size mismatch: {len(rec)}"
    return rec

# ---------------------------------------------------------------------------
# UF2 block generation
# ---------------------------------------------------------------------------

def _make_uf2_block(target_addr: int, data: bytes,
                    block_num: int, total_blocks: int) -> bytes:
    assert len(data) <= UF2_PAYLOAD_SIZE
    payload = data + b"\x00" * (UF2_PAYLOAD_SIZE - len(data))  # pad to 256
    padding = b"\x00" * (476 - UF2_PAYLOAD_SIZE)               # remainder to 476
    hdr = struct.pack("<IIIIIIII",
                      UF2_MAGIC0,
                      UF2_MAGIC1,
                      UF2_FLAG_FAMILYID,
                      target_addr,
                      UF2_PAYLOAD_SIZE,
                      block_num,
                      total_blocks,
                      RP2350_FAMILY_ID)
    block = hdr + payload + padding + struct.pack("<I", UF2_MAGIC_END)
    assert len(block) == 512
    return block


def _region_to_blocks(data: bytes, flash_base_addr: int,
                       block_offset: int, total_blocks: int) -> list[bytes]:
    """Slice `data` into 256-byte UF2 blocks starting at `flash_base_addr`."""
    blocks = []
    for i in range(0, len(data), UF2_PAYLOAD_SIZE):
        chunk = data[i:i + UF2_PAYLOAD_SIZE]
        target = flash_base_addr + i
        blocks.append(_make_uf2_block(target, chunk,
                                      block_offset + len(blocks),
                                      total_blocks))
    return blocks


def _pad_to_block(data: bytes) -> bytes:
    """Pad `data` to a multiple of UF2_PAYLOAD_SIZE with 0xFF bytes."""
    remainder = len(data) % UF2_PAYLOAD_SIZE
    if remainder:
        data += b"\xff" * (UF2_PAYLOAD_SIZE - remainder)
    return data

# ---------------------------------------------------------------------------
# Mapper type detection from filename
# ---------------------------------------------------------------------------

def detect_mapper(filename: str) -> str | None:
    """
    Detect the canonical mapper type from the ROM filename.

    Convention: <title>.<MAPPER_SUFFIX>.rom
    where MAPPER_SUFFIX is e.g. MAPPER_LINEAR, MAPPER_KONAMI, KONAMI, etc.
    Matching is case-insensitive.
    """
    stem = os.path.splitext(os.path.basename(filename))[0]
    parts = stem.rsplit(".", 1)
    if len(parts) != 2:
        return None
    suffix = parts[1].lower()
    if suffix.startswith("mapper_"):
        suffix = suffix[len("mapper_"):]
    return _MAPPER_ALIASES.get(suffix)

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("rom_file",
                        help="Input ROM file")
    parser.add_argument("output", nargs="?",
                        help="Output UF2 file (default: <rom_stem>.uf2)")
    parser.add_argument("--mapper",
                        help=("Mapper type override.  Supported: "
                              + ", ".join(SUPPORTED_MAPPERS)))
    parser.add_argument("--id", dest="payload_id", default=None,
                        help=("Payload/collection ID "
                              f"(≤ {PAYLOAD_ID_MAX} chars; "
                              "default: derived from filename)"))
    parser.add_argument("--subslot", type=int, default=0,
                        help="MSX subslot 0–3 (default: 0)")
    parser.add_argument("--publisher", default="local",
                        help="Publisher ID string (default: local)")
    args = parser.parse_args()

    rom_path = args.rom_file
    if not os.path.exists(rom_path):
        sys.exit(f"error: ROM file not found: {rom_path}")

    # Resolve mapper type.
    mapper_type = args.mapper
    if mapper_type is None:
        mapper_type = detect_mapper(rom_path)
    if mapper_type is None:
        sys.exit(
            f"error: cannot detect mapper type from '{os.path.basename(rom_path)}'.\n"
            f"       Use --mapper <type>.  Supported: {', '.join(SUPPORTED_MAPPERS)}"
        )
    mapper_type = _MAPPER_ALIASES.get(mapper_type.lower(), mapper_type.lower())
    if mapper_type not in SUPPORTED_MAPPERS:
        sys.exit(
            f"error: unknown mapper type '{mapper_type}'.\n"
            f"       Supported: {', '.join(SUPPORTED_MAPPERS)}"
        )

    if not (0 <= args.subslot <= 3):
        sys.exit("error: --subslot must be 0–3")

    # Resolve payload ID.
    if args.payload_id:
        payload_id = args.payload_id
    else:
        stem = os.path.splitext(os.path.basename(rom_path))[0]
        m = re.match(r"^(.+)\.(\w+)$", stem, re.IGNORECASE)
        if m:
            suffix = m.group(2).lower()
            if suffix.startswith("mapper_"):
                suffix = suffix[len("mapper_"):]
            if suffix in _MAPPER_ALIASES:
                stem = m.group(1)
        # Sanitise: keep alphanumerics, dots, hyphens, underscores.
        payload_id = re.sub(r"[^a-zA-Z0-9._-]", "_", stem)
    payload_id = payload_id[:PAYLOAD_ID_MAX]

    kv_key = KV_PAYLOAD_PREFIX + payload_id
    if len(kv_key) > KV_MAX_KEY_LEN:
        sys.exit(
            f"error: KV key '{kv_key}' ({len(kv_key)} chars) exceeds "
            f"{KV_MAX_KEY_LEN}-byte limit.\n"
            "       Use --id to specify a shorter payload ID."
        )

    output_path = args.output or (os.path.splitext(rom_path)[0] + ".uf2")

    # Read ROM.
    with open(rom_path, "rb") as f:
        rom_data = f.read()
    rom_size = len(rom_data)

    print(f"ROM:     {rom_path}")
    print(f"         {rom_size} bytes  mapper={mapper_type}  subslot={args.subslot}")
    print(f"ID:      {payload_id}")

    # ------------------------------------------------------------------
    # Build KV region binary.
    # ------------------------------------------------------------------

    kv_records = b""

    # 1. col.state = "active"
    kv_records += make_kv_record(KV_COL_STATE, b"active")

    # 2. col.record = CollectionRecord
    col_rec = make_collection_record(
        collection_id      = payload_id,
        version            = "1.0.0",
        publisher_id       = args.publisher,
        title              = payload_id,
        default_payload_id = payload_id,
        payload_count      = 1,
        boot_mode          = 1,   # direct boot
    )
    kv_records += make_kv_record(KV_COL_RECORD, col_rec)

    # 3. pl.<id> = PayloadRecord  (data_size = actual ROM size)
    pl_rec = make_payload_record(
        payload_id        = payload_id,
        mapper_type       = mapper_type,
        subslot           = args.subslot,
        data_flash_offset = FLASH_CONTENT_DATA_OFS,
        data_size         = rom_size,
    )
    kv_records += make_kv_record(kv_key, pl_rec)

    # Pad both regions to UF2_PAYLOAD_SIZE multiples.
    kv_padded  = _pad_to_block(kv_records)
    rom_padded = _pad_to_block(rom_data)

    # ------------------------------------------------------------------
    # Generate UF2.
    # ------------------------------------------------------------------

    kv_block_count  = len(kv_padded)  // UF2_PAYLOAD_SIZE
    rom_block_count = len(rom_padded) // UF2_PAYLOAD_SIZE
    total_blocks    = kv_block_count + rom_block_count

    kv_flash_addr  = XIP_BASE + FLASH_SYSTEM_KV_OFS    # 0x10200000
    rom_flash_addr = XIP_BASE + FLASH_CONTENT_DATA_OFS  # 0x10600000

    kv_blocks  = _region_to_blocks(kv_padded,  kv_flash_addr,  0,              total_blocks)
    rom_blocks = _region_to_blocks(rom_padded, rom_flash_addr, kv_block_count, total_blocks)

    with open(output_path, "wb") as f:
        for block in kv_blocks + rom_blocks:
            f.write(block)

    print(f"\nOutput:  {output_path}")
    print(f"         KV  region: {kv_block_count:4d} blocks @ 0x{kv_flash_addr:08X}")
    print(f"         ROM region: {rom_block_count:4d} blocks @ 0x{rom_flash_addr:08X}")
    print(f"         Total:      {total_blocks:4d} blocks ({total_blocks * 512} bytes)")
    print(f"\nFlash with:")
    print(f"  picotool load {output_path}")


if __name__ == "__main__":
    main()

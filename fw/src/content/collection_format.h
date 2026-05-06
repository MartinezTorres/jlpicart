#pragma once
// collection_format.h — Collection bundle layout constants and types.

#include <cstdint>
#include <cstddef>

// ---------------------------------------------------------------------------
// Size bounds
// ---------------------------------------------------------------------------

static constexpr size_t COL_ID_MAX             = 64;   // collection_id maxLength
static constexpr size_t COL_VERSION_MAX        = 32;   // version maxLength
static constexpr size_t COL_TITLE_MAX          = 128;  // title maxLength
static constexpr size_t PUB_ID_MAX             = 64;   // publisher_id maxLength
static constexpr size_t PAYLOAD_ID_MAX         = 64;   // payload_id maxLength
static constexpr size_t PAYLOAD_PATH_MAX       = 200;  // path maxLength
static constexpr size_t PAYLOAD_MAPPER_TYPE_MAX = 24;  // "rom_32k_mirrored" = 16 chars max
static constexpr size_t SIG_KEY_ID_MAX         = 64;   // key_id in bundle.sig
static constexpr size_t BUNDLE_MAX_FILES       = 8;    // max files listed in bundle.sig
static constexpr size_t MANIFEST_MAX_PAYLOADS  = 4;    // max payload entries parsed
static constexpr size_t MANIFEST_BYTES_MAX     = 4096; // max bytes for read_file of manifest
static constexpr size_t SIG_ENV_BYTES_MAX      = 2048; // max bytes for read_file of bundle.sig
static constexpr size_t PAYLOAD_DEVICES_MAX    = 4;    // max device entries per payload

// ---------------------------------------------------------------------------
// Bundle file paths (relative to bundle root)
// ---------------------------------------------------------------------------

static constexpr const char* BUNDLE_MANIFEST_FILE = "manifest.json";
static constexpr const char* BUNDLE_SIG_FILE      = "bundle.sig";

// ---------------------------------------------------------------------------
// Bundle signature envelope — parsed from bundle.sig.
// ---------------------------------------------------------------------------

struct BundleFileHash {
    char    path[PAYLOAD_PATH_MAX]; // relative path within bundle
    uint8_t sha256[32];             // binary SHA-256 (decoded from hex string in JSON)
};

struct BundleSigEnvelope {
    char           sig_schema[32];           // "jlpicart.signature.v1"
    char           alg[32];                  // "ecdsa_secp256k1_sha256"
    char           key_id[SIG_KEY_ID_MAX];
    BundleFileHash files[BUNDLE_MAX_FILES];
    uint8_t        file_count;
    uint8_t        signature[144];           // base64-decoded ECDSA sig (DER ≤ ~72 bytes)
    uint8_t        sig_len;
    bool           has_envelope;             // false if bundle.sig was absent
};

// ---------------------------------------------------------------------------
// Compact record for the installed Collection, stored as
// 1:/collections/{col_id}/collection.bin on the FAT volume.
// ---------------------------------------------------------------------------

#pragma pack(push, 1)
struct CollectionRecord {
    char    collection_id[COL_ID_MAX];           // 64 bytes
    char    version[COL_VERSION_MAX];            // 32 bytes
    char    publisher_id[PUB_ID_MAX];            // 64 bytes
    char    title[COL_TITLE_MAX];                // 128 bytes
    uint8_t boot_mode;                           //  1 byte  (0=menu_first, 1=direct)
    char    default_payload_id[PAYLOAD_ID_MAX];  // 64 bytes
    uint8_t payload_count;                       //  1 byte
    uint8_t _pad[3];                             //  3 bytes (alignment)
    // Total: 64+32+64+128+1+64+1+3 = 357 bytes
};
#pragma pack(pop)
static_assert(sizeof(CollectionRecord) == 357, "CollectionRecord layout has changed");

// ---------------------------------------------------------------------------
// Per-device record embedded in a PayloadRecord.
// Describes one collection device (PSG, OPL4, SCC, …) required by a payload.
// ---------------------------------------------------------------------------

#pragma pack(push, 1)
struct PayloadDeviceRecord {
    uint8_t type;                      //  1 byte  (PeripheralDescriptor.type_id)
    uint8_t subslot;                   //  1 byte  (0–3; for memory-mapped devices)
    uint8_t optional;                  //  1 byte  (0=required, 1=optional)
    uint8_t _pad;                      //  1 byte
    char    params[PAYLOAD_ID_MAX];    // 64 bytes (peripheral-specific pass-through params)
    // Total: 68 bytes
};
#pragma pack(pop)
static_assert(sizeof(PayloadDeviceRecord) == 68, "PayloadDeviceRecord layout has changed");

// ---------------------------------------------------------------------------
// Per-payload record, stored as 1:/collections/{col_id}/payload_{id}.bin.
//
// Written by Installer::run() at install time.  Read at boot by ContentStore.
//
// data_flash_offset: offset from the start of external flash (not XIP base).
//   On RP2350: XIP pointer = 0x10000000 + data_flash_offset.
// data_size: 0 means no ROM data was written to flash (bus wiring skipped).
// ---------------------------------------------------------------------------

#pragma pack(push, 1)
struct PayloadRecord {
    char     payload_id[PAYLOAD_ID_MAX];           // 64 bytes
    char     mapper_type[PAYLOAD_MAPPER_TYPE_MAX]; // 24 bytes
    uint8_t  subslot;                              //  1 byte  (0–3)
    uint8_t  device_count;                         //  1 byte  (number of valid devices[])
    uint8_t  _pad[2];                              //  2 bytes
    uint32_t data_flash_offset;                    //  4 bytes (offset from flash start)
    uint32_t data_size;                            //  4 bytes (0 = not yet written)
    PayloadDeviceRecord devices[PAYLOAD_DEVICES_MAX]; // 4×68 = 272 bytes
    // Total: 64 + 24 + 1 + 1 + 2 + 4 + 4 + 272 = 372 bytes
};
#pragma pack(pop)
static_assert(sizeof(PayloadRecord) == 372, "PayloadRecord layout has changed");

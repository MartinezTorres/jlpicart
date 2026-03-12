#pragma once
// collection_format.h — Collection bundle layout constants and types.
// See spec.md §6.1 (Collection format contract v1) and §6.2 (Manifest contract).

#include <cstdint>
#include <cstddef>

// ---------------------------------------------------------------------------
// Size bounds (from spec.md §6.3 JSON schemas)
// ---------------------------------------------------------------------------

static constexpr size_t COL_ID_MAX             = 64;   // collection_id maxLength
static constexpr size_t COL_VERSION_MAX        = 32;   // version maxLength
static constexpr size_t COL_TITLE_MAX          = 128;  // title maxLength
static constexpr size_t PUB_ID_MAX             = 64;   // publisher_id maxLength
static constexpr size_t PAYLOAD_ID_MAX         = 64;   // payload_id maxLength
static constexpr size_t PAYLOAD_PATH_MAX       = 200;  // path maxLength (spec §6.1)
static constexpr size_t PAYLOAD_MAPPER_TYPE_MAX = 24;  // "rom_32k_mirrored" = 16 chars max
static constexpr size_t SIG_KEY_ID_MAX         = 64;   // key_id in bundle.sig
static constexpr size_t BUNDLE_MAX_FILES       = 8;    // max files listed in bundle.sig
static constexpr size_t MANIFEST_MAX_PAYLOADS  = 4;    // max payload entries parsed
static constexpr size_t MANIFEST_BYTES_MAX     = 4096; // max bytes for read_file of manifest
static constexpr size_t SIG_ENV_BYTES_MAX      = 2048; // max bytes for read_file of bundle.sig

// ---------------------------------------------------------------------------
// Bundle file paths (relative to bundle root, per spec §6.1)
// ---------------------------------------------------------------------------

static constexpr const char* BUNDLE_MANIFEST_FILE = "manifest.json";
static constexpr const char* BUNDLE_SIG_FILE      = "bundle.sig";

// ---------------------------------------------------------------------------
// KvStore key names for collection state (spec §6.1, §11.1 atomicity rules)
// ---------------------------------------------------------------------------

static constexpr const char* KV_COL_STATE  = "col.state";  // "active" or "pending"
static constexpr const char* KV_COL_RECORD = "col.record"; // CollectionRecord bytes

static constexpr const char* COL_STATE_ACTIVE  = "active";
static constexpr const char* COL_STATE_PENDING = "pending";

// ---------------------------------------------------------------------------
// Bundle signature envelope — parsed from bundle.sig.
// See spec.md §6.2 "Bundle signature envelope (minimal v1)".
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
// Compact on-flash record for the installed Collection (stored in KvStore).
// The full payload list must be re-read from the source at launch time.
// See spec.md §6.2 "Collection Manifest schema".
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
    uint8_t _pad[3];                             //  3 bytes (makes total a multiple of 4)
    // Total: 64+32+64+128+1+64+1+3 = 357 bytes — fits in KV_MAX_VAL_LEN (512)
};
#pragma pack(pop)
static_assert(sizeof(CollectionRecord) == 357, "CollectionRecord layout has changed");
static_assert(sizeof(CollectionRecord) <= 512,
              "CollectionRecord must fit in KV_MAX_VAL_LEN (512)");

// ---------------------------------------------------------------------------
// Per-payload runtime record (stored in KvStore under "pl.<payload_id>").
//
// Written by Installer::run() at commit time and by populate_flash.py when
// pre-populating flash from a ROM file.  Read at boot by ContentStore.
//
// data_flash_offset: offset from the start of external flash (not XIP base).
//   On RP2350: XIP pointer = 0x10000000 + data_flash_offset.
// data_size: size of ROM/RAM data in bytes.
//   0 means the ROM has not been written to flash yet (bus wiring skipped).
//
// KV key: KV_PAYLOAD_PREFIX + payload_id.  payload_id must be ≤ 45 chars
// so the full key fits within KV_MAX_KEY_LEN (48).
// ---------------------------------------------------------------------------

static constexpr const char* KV_PAYLOAD_PREFIX = "pl.";  // 3-char prefix
// Full KV key = KV_PAYLOAD_PREFIX + payload_id; payload_id must be ≤ 45 chars
// so the combined key fits within KV_MAX_KEY_LEN (48).

#pragma pack(push, 1)
struct PayloadRecord {
    char     payload_id[PAYLOAD_ID_MAX];           // 64 bytes
    char     mapper_type[PAYLOAD_MAPPER_TYPE_MAX]; // 24 bytes
    uint8_t  subslot;                              //  1 byte  (0–3)
    uint8_t  _pad[3];                              //  3 bytes (alignment)
    uint32_t data_flash_offset;                    //  4 bytes (offset from flash start)
    uint32_t data_size;                            //  4 bytes (0 = not yet written)
    // Total: 64 + 24 + 1 + 3 + 4 + 4 = 100 bytes
};
#pragma pack(pop)
static_assert(sizeof(PayloadRecord) == 100, "PayloadRecord layout has changed");
// KV_MAX_VAL_LEN = 512; PayloadRecord (100) fits comfortably.
static_assert(sizeof(PayloadRecord) <= 512,
              "PayloadRecord must fit in KvStore value limit (512)");

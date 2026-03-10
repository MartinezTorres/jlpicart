#pragma once
// collection_format.h — Collection bundle layout constants and types.
// See spec.md §6.1 (Collection format contract v1) and §6.2 (Manifest contract).

#include <cstdint>
#include <cstddef>

// ---------------------------------------------------------------------------
// Size bounds (from spec.md §6.3 JSON schemas)
// ---------------------------------------------------------------------------

static constexpr size_t COL_ID_MAX            = 64;   // collection_id maxLength
static constexpr size_t COL_VERSION_MAX        = 32;   // version maxLength
static constexpr size_t COL_TITLE_MAX          = 128;  // title maxLength
static constexpr size_t PUB_ID_MAX             = 64;   // publisher_id maxLength
static constexpr size_t PAYLOAD_ID_MAX         = 64;   // payload_id maxLength
static constexpr size_t PAYLOAD_PATH_MAX       = 200;  // path maxLength (spec §6.1)
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

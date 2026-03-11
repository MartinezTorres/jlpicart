#pragma once
// manifest.h — In-memory representation of a parsed Collection Manifest.
// See spec.md §6.2 and §6.3.1 (Collection manifest JSON Schema).

#include "content/collection_format.h"
#include <cstdint>

// Max capability entries per payload and max capability id length.
// Matches CAP_ID_MAX in allocator/allocator.h.
static constexpr size_t PAYLOAD_CAPS_MAX    = 4;
static constexpr size_t PAYLOAD_CAP_ID_MAX  = 64;

struct PayloadEntry {
    char payload_id[PAYLOAD_ID_MAX];
    char path[PAYLOAD_PATH_MAX];
    char title[COL_TITLE_MAX];

    // Capability requirements (Stage 8).  Parsed from payload JSON fields
    // "required_capabilities" and "optional_capabilities" (arrays of strings).
    // Hard requirements cause launch failure if not activated.
    char    required_capabilities[PAYLOAD_CAPS_MAX][PAYLOAD_CAP_ID_MAX];
    uint8_t required_cap_count;
    char    optional_capabilities[PAYLOAD_CAPS_MAX][PAYLOAD_CAP_ID_MAX];
    uint8_t optional_cap_count;
};

struct CollectionManifest {
    char         schema[32];          // must be "jlpicart.collection.v1"
    char         collection_id[COL_ID_MAX];
    char         version[COL_VERSION_MAX];
    char         publisher_id[PUB_ID_MAX];
    char         publisher_name[COL_TITLE_MAX];
    char         title[COL_TITLE_MAX];
    uint8_t      boot_mode;           // 0 = menu_first (default), 1 = direct
    char         default_payload_id[PAYLOAD_ID_MAX];
    PayloadEntry payloads[MANIFEST_MAX_PAYLOADS];
    uint8_t      payload_count;
};

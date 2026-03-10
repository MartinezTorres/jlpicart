#pragma once
// manifest.h — In-memory representation of a parsed Collection Manifest.
// See spec.md §6.2 and §6.3.1 (Collection manifest JSON Schema).

#include "content/collection_format.h"
#include <cstdint>

struct PayloadEntry {
    char payload_id[PAYLOAD_ID_MAX];
    char path[PAYLOAD_PATH_MAX];
    char title[COL_TITLE_MAX];
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

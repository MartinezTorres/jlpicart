#pragma once
// manifest.h — In-memory representation of a parsed Collection Manifest.

#include "content/collection_format.h"
#include "peripherals/peripheral_descriptor.h"
#include <cstdint>

// Max capability entries per payload and max capability id length.
// Matches CAP_ID_MAX in allocator/allocator.h.
static constexpr size_t PAYLOAD_CAPS_MAX    = 4;
static constexpr size_t PAYLOAD_CAP_ID_MAX  = 64;

// PAYLOAD_MAPPER_TYPE_MAX and PAYLOAD_DEVICES_MAX are defined in collection_format.h.

// One device entry parsed from a payload's "devices" array in the manifest.
struct ManifestDeviceEntry {
    const PeripheralDescriptor* descriptor; // resolved at parse time; nullptr = unknown
    uint8_t subslot;                        // MSX subslot (0–3); for memory-mapped devices
    bool    optional;                       // true → user may choose to deactivate
    char    params[PAYLOAD_ID_MAX];         // peripheral-specific pass-through params
};

struct PayloadEntry {
    char payload_id[PAYLOAD_ID_MAX];
    char path[PAYLOAD_PATH_MAX];
    char title[COL_TITLE_MAX];

    // Capability requirements.  Hard requirements cause launch failure if not activated.
    char    required_capabilities[PAYLOAD_CAPS_MAX][PAYLOAD_CAP_ID_MAX];
    uint8_t required_cap_count;
    char    optional_capabilities[PAYLOAD_CAPS_MAX][PAYLOAD_CAP_ID_MAX];
    uint8_t optional_cap_count;

    // Mapper configuration.  mapper_type="" and subslot=0 are defaults.
    char    mapper_type[PAYLOAD_MAPPER_TYPE_MAX];  // "" = not specified (NONE)
    uint8_t subslot;                               // 0–3 (default 0)

    // Collection devices.  Parsed from payload JSON field "devices".
    ManifestDeviceEntry devices[PAYLOAD_DEVICES_MAX];
    uint8_t device_count;
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

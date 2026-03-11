// activation.cc — Stage 8 activation helpers.

#include "spine/activation.h"
#include <cstring>

RequestedCapabilities requested_from_manifest(const CollectionManifest& manifest,
                                               uint8_t payload_index)
{
    RequestedCapabilities req = {};

    if (payload_index >= manifest.payload_count) return req;

    const PayloadEntry& pe = manifest.payloads[payload_index];

    for (uint8_t i = 0; i < pe.required_cap_count && req.required_count < REQUESTED_CAPS_MAX; ++i) {
        strncpy(req.required[req.required_count], pe.required_capabilities[i], CAP_ID_MAX - 1u);
        req.required[req.required_count][CAP_ID_MAX - 1u] = '\0';
        ++req.required_count;
    }

    for (uint8_t i = 0; i < pe.optional_cap_count && req.optional_count < REQUESTED_CAPS_MAX; ++i) {
        strncpy(req.optional_caps[req.optional_count], pe.optional_capabilities[i], CAP_ID_MAX - 1u);
        req.optional_caps[req.optional_count][CAP_ID_MAX - 1u] = '\0';
        ++req.optional_count;
    }

    return req;
}

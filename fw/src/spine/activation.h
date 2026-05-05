#pragma once
// activation.h — Convert a manifest payload entry to RequestedCapabilities
// for the Allocator.

#include "spine/allocator.h"
#include "content/manifest.h"

// Compile-time guard: CAP_ID_MAX and PAYLOAD_CAP_ID_MAX must stay in sync.
// If either changes, this fires immediately rather than silently corrupting
// capability id strings in requested_from_manifest().
static_assert(CAP_ID_MAX == PAYLOAD_CAP_ID_MAX,
              "CAP_ID_MAX and PAYLOAD_CAP_ID_MAX must be equal");

// Build a RequestedCapabilities struct from one payload entry in a manifest.
//
// If payload_index is out of range, or the payload has no capability
// declarations, returns an empty struct (required_count=0, optional_count=0,
// all_allowed=false).  That is a valid input to the Allocator — it produces a
// plan where nothing is requested (all capabilities skipped).
RequestedCapabilities requested_from_manifest(const CollectionManifest& manifest,
                                               uint8_t payload_index);

#pragma once
// manifest_parser.h — Strict JSON manifest parser for Collection bundles.
//
// Parses manifest.json and bundle.sig into their respective structs.
// Unknown JSON fields are silently ignored for forward compatibility.
// Any schema violation (wrong type, missing required field, bad value)
// returns COLLECTION_BAD_MANIFEST.
//
// See spec.md §6.2 (Manifest and configuration schema contract v1).

#include "diag/diag.h"
#include "content/manifest.h"

// Parse a manifest.json document into `out`.
// `json` must be a valid UTF-8 string of `len` bytes.
// Required fields: format_version=="1.0", collection_id, version, payloads (≥1).
DiagStatus parse_collection_manifest(const char* json, size_t len,
                                      CollectionManifest& out);

// Parse a bundle.sig document into `out`.
// `json` must be a valid UTF-8 string of `len` bytes.
// sha256 hex strings are decoded into binary; signature base64 is decoded.
DiagStatus parse_bundle_sig(const char* json, size_t len,
                             BundleSigEnvelope& out);

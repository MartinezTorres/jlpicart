#pragma once
// bundle_sig_verify.h — ed25519 signature verification for collection bundles.
//
// Publisher anchor key is stored at 1:/system/pub_anchor.bin (32 bytes).
// FatVolume must be mounted when calling policy_get_publisher_anchor().

#include "content/collection_format.h"
#include <cstdint>

// Load the publisher's ed25519 public key from 1:/system/pub_anchor.bin.
// Returns true if the file exists and is exactly 32 bytes.
bool policy_get_publisher_anchor(uint8_t anchor_out[32]);

// Verify the ed25519 signature in env against pubkey[32].
// manifest_sha256 is the SHA-256 of the manifest.json bytes (the signed message).
bool bundle_sig_verify(const BundleSigEnvelope& env,
                       const uint8_t pubkey[32],
                       const uint8_t manifest_sha256[32]);

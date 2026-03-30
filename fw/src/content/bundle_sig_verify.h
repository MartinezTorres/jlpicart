#pragma once
// bundle_sig_verify.h — ed25519 signature verification for collection bundles.
//
// Verifies a bundle.sig envelope against a 32-byte ed25519 public key anchor.
// The signed message is the SHA-256 of the manifest.json bytes (32 bytes),
// which is already computed by Installer::run() as result_out.manifest_sha256.
//
// Algorithm string: "ed25519"
// Signature field : BundleSigEnvelope.signature[0..63] (first 64 bytes)
// Public key      : 32-byte ed25519 key stored in KvStore under "pub.anchor"
//
// See spec.md §6.2 "Bundle signature envelope" and bootstrapping.md Stage 31.

#include "content/collection_format.h"
#include "storage/kv_store.h"
#include <cstdint>

// Look up the publisher's ed25519 public key from KvStore ("pub.anchor" key).
// Returns true if the key exists and is exactly 32 bytes; copies it to anchor_out.
// Returns false (and leaves anchor_out unchanged) if absent or wrong size.
bool policy_get_publisher_anchor(KvStore& kv, uint8_t anchor_out[32]);

// Verify the ed25519 signature in env against pubkey[32].
// manifest_sha256 is the SHA-256 of the manifest.json bytes (the signed message).
// Returns true if the signature is valid; false otherwise.
// Only the "ed25519" algorithm is supported; any other alg returns false.
bool bundle_sig_verify(const BundleSigEnvelope& env,
                       const uint8_t pubkey[32],
                       const uint8_t manifest_sha256[32]);

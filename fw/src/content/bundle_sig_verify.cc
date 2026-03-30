// bundle_sig_verify.cc — ed25519 bundle signature verification.

#include "content/bundle_sig_verify.h"
#include "crypto/monocypher/monocypher.h"
#include <cstring>

bool policy_get_publisher_anchor(KvStore& kv, uint8_t anchor_out[32]) {
    uint16_t len = 0;
    DiagStatus s = kv.get("pub.anchor", anchor_out, &len, 32u);
    return s.ok() && len == 32u;
}

bool bundle_sig_verify(const BundleSigEnvelope& env,
                       const uint8_t pubkey[32],
                       const uint8_t manifest_sha256[32])
{
    // Only ed25519 is supported.
    if (strncmp(env.alg, "ed25519", sizeof(env.alg)) != 0) return false;

    // ed25519 signature is always 64 bytes.
    if (env.sig_len != 64u) return false;

    // crypto_eddsa_check returns 0 on success, -1 on failure.
    return crypto_eddsa_check(env.signature, pubkey,
                              manifest_sha256, 32u) == 0;
}

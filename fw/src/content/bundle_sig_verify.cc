// bundle_sig_verify.cc — ed25519 bundle signature verification.

#include "content/bundle_sig_verify.h"
#include "filesystem/fat_util.h"
#include "crypto/monocypher/monocypher.h"
#include <cstring>

static constexpr const char* ANCHOR_PATH = "1:/system/pub_anchor.bin";

bool policy_get_publisher_anchor(uint8_t anchor_out[32]) {
    size_t actual = 0;
    return fat_read_file(ANCHOR_PATH, anchor_out, 32u, &actual) && actual == 32u;
}

bool bundle_sig_verify(const BundleSigEnvelope& env,
                       const uint8_t pubkey[32],
                       const uint8_t manifest_sha256[32])
{
    if (strncmp(env.alg, "ed25519", sizeof(env.alg)) != 0) return false;
    if (env.sig_len != 64u) return false;
    return crypto_eddsa_check(env.signature, pubkey,
                               manifest_sha256, 32u) == 0;
}

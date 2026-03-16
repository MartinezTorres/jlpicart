#pragma once
// device_identity.h — Device Identity Key (DIK) management (Stage 22).
//
// The DIK is an ed25519 keypair generated on first boot and persisted in the
// SYSTEM_KV partition.  The private key is stored encrypted under a key
// derived from the RP2350 OTP device secret (SMK derivation per spec §10).
// On development units without an OTP secret, it is stored with a "seed-only
// unprovisioned" flag.
//
// In JLPICART_HOST_TEST builds, OpenSSL is used for ed25519 operations and
// the private key is stored in plaintext (no OTP / encryption available).
//
// Spec references: §10 "Key hierarchy and roles (DIK)",
//                  §11.1 "Initialization and provisioning",
//                  §7.2 "System service method 0x01 GET_DEVICE_ID".

#include "diag/diag.h"
#include <cstdint>
#include <cstddef>

class KvStore;

// Compute a 16-byte scoped device ID from a 32-byte public key.
// scope 0 = stable device ID ("dev" label)
// scope 1 = collection-scoped ("col" label)
// scope 2 = publisher-scoped ("pub" label)
void device_identity_scoped_id(const uint8_t* pub_key_32, uint8_t scope,
                                uint8_t* out_16);

// Key sizes (ed25519).
static constexpr size_t DIK_PUB_KEY_LEN  = 32u; // public key bytes
static constexpr size_t DIK_PRIV_KEY_LEN = 64u; // private key bytes (seed || public key)
static constexpr size_t DIK_SIG_LEN      = 64u; // signature bytes

// KV key names.
static constexpr const char* KV_DIK_PUB   = "dik.pub";   // 32-byte public key (plaintext)
static constexpr const char* KV_DIK_PRIV  = "dik.priv";  // 64-byte private key (encrypted/plaintext)
static constexpr const char* KV_DIK_FLAGS = "dik.flags";  // uint8_t: 1=provisioned (OTP), 0=seed-only

class DeviceIdentity {
public:
    // Attempt to load an existing DIK from kv.  If no key is found, generate
    // a new keypair using TRNG (hardware) or deterministic seed (host tests),
    // then store it.  Idempotent: calling twice returns the same public key.
    DiagStatus init_or_load(KvStore& kv);

    bool initialized() const { return initialized_; }

    // Copy the 32-byte public key into out_32.
    void public_key(uint8_t* out_32) const;

    // Sign msg[0..len-1] and write the 64-byte signature into sig_out_64.
    DiagStatus sign(const uint8_t* msg, size_t len, uint8_t* sig_out_64) const;

    // True if the OTP device secret was present at keygen time (provisioned unit).
    bool is_provisioned() const { return provisioned_; }

private:
    bool    initialized_ = false;
    bool    provisioned_ = false;
    uint8_t pub_key_[DIK_PUB_KEY_LEN]  = {};
    uint8_t priv_key_[DIK_PRIV_KEY_LEN] = {};

    // Generate a new keypair using available entropy.
    // Fills priv_key_[0..63] and pub_key_[0..31].
    DiagStatus generate_keypair();

    // Persist the current keypair to kv.
    DiagStatus store(KvStore& kv) const;
};

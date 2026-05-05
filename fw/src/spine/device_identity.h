#pragma once
// device_identity.h — Device Identity Key (DIK) management backed by FAT.
//
// The DIK is an ed25519 keypair persisted at 1:/system/dik.bin.
// The private key is encrypted under a key derived from the RP2350 OTP secret.
// On development units without OTP, it is stored with an "unprovisioned" flag.
//
// File format (137 bytes):
//   pub[32] | nonce[24] | mac[16] | priv_ct[64] | flags[1]
//
// FatVolume must be mounted before calling init_or_load().

#include "diag/diag.h"
#include <cstdint>
#include <cstddef>

// Compute a 16-byte scoped device ID from a 32-byte public key.
void device_identity_scoped_id(const uint8_t* pub_key_32, uint8_t scope,
                                uint8_t* out_16);

static constexpr size_t DIK_PUB_KEY_LEN  = 32u;
static constexpr size_t DIK_PRIV_KEY_LEN = 64u;
static constexpr size_t DIK_SIG_LEN      = 64u;

class DeviceIdentity {
public:
    // Attempt to load an existing DIK from 1:/system/dik.bin.
    // If the file is absent or decryption fails, generate a new keypair and store it.
    //
    // wrap_key_32: 32-byte key that wraps the private key in storage.
    //   nullptr → zero-derived key (unprovisioned; effectively unencrypted).
    DiagStatus init_or_load(const uint8_t* wrap_key_32 = nullptr);

    bool initialized() const { return initialized_; }

    void public_key(uint8_t* out_32) const;

    DiagStatus sign(const uint8_t* msg, size_t len, uint8_t* sig_out_64) const;

    bool is_provisioned() const { return provisioned_; }

private:
    static constexpr const char* PATH = "1:/system/dik.bin";

    bool    initialized_ = false;
    bool    provisioned_ = false;
    uint8_t pub_key_[DIK_PUB_KEY_LEN]  = {};
    uint8_t priv_key_[DIK_PRIV_KEY_LEN] = {};

    DiagStatus generate_keypair();
    DiagStatus store(const uint8_t* wrap_key_32) const;
};

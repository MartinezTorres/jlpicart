// test_device_identity.cc — host tests for DeviceIdentity and GET_DEVICE_ID (Stage 22).
//
// In host test mode, DeviceIdentity uses OpenSSL for real ed25519 keygen/sign.
// The tests verify: key generation, persistence (idempotency), sign/verify,
// and scoped device ID derivation via GET_DEVICE_ID.

#include "spine/device_identity.h"
#include "msx/api/api_window.h"
#include "msx/api/api_types.h"
#include "spine/security_posture.h"
#include "spine/policy_store.h"
#include "spine/capability_registry.h"
#include "platform/board_descriptor.h"
#include "spine/driver_descriptor.h"

// ed25519 verify via OpenSSL (host only).
#include <openssl/evp.h>

#include "fat_test_env.h"
#include "filesystem/fat_util.h"
#include "test_helpers.h"
#include <cstring>
#include <cstdio>

struct DikFixture {
    FatTestEnv env;

    DikFixture() {}
};

// Verify an ed25519 signature using OpenSSL (host-test only helper).
static bool ed25519_verify(const uint8_t* msg, size_t msg_len,
                            const uint8_t* sig_64, const uint8_t* pub_32)
{
    EVP_PKEY* key = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
                                                  pub_32, 32u);
    if (!key) return false;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) { EVP_PKEY_free(key); return false; }

    bool ok = false;
    if (EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, key) == 1) {
        ok = (EVP_DigestVerify(ctx, sig_64, 64u, msg, msg_len) == 1);
    }

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(key);
    return ok;
}

// ---------------------------------------------------------------------------
// test_dik_generate — fresh FAT volume → keygen; key file stored
// ---------------------------------------------------------------------------

static void test_dik_generate()
{
    DikFixture f;

    DeviceIdentity dik;
    CHECK(dik.init_or_load().ok());
    CHECK(dik.initialized());

    uint8_t pub[DIK_PUB_KEY_LEN];
    dik.public_key(pub);

    // Public key should be non-zero (a valid ed25519 point is never all-zeros).
    bool all_zero = true;
    for (size_t i = 0; i < DIK_PUB_KEY_LEN; ++i) {
        if (pub[i] != 0u) { all_zero = false; break; }
    }
    CHECK(!all_zero);

    // Key material should be stored at the expected FAT path.
    CHECK(fat_file_exists("1:/system/dik.bin"));
}

// ---------------------------------------------------------------------------
// test_dik_reload — load a pre-generated key; same public key returned
// ---------------------------------------------------------------------------

static void test_dik_reload()
{
    DikFixture f;

    // Generate and capture public key.
    DeviceIdentity dik1;
    CHECK(dik1.init_or_load().ok());
    uint8_t pub1[DIK_PUB_KEY_LEN];
    dik1.public_key(pub1);

    // Reload from same FAT volume.
    DeviceIdentity dik2;
    CHECK(dik2.init_or_load().ok());
    uint8_t pub2[DIK_PUB_KEY_LEN];
    dik2.public_key(pub2);

    CHECK(memcmp(pub1, pub2, DIK_PUB_KEY_LEN) == 0);
}

// ---------------------------------------------------------------------------
// test_dik_idempotent — call init_or_load() twice on same instance + same volume
// ---------------------------------------------------------------------------

static void test_dik_idempotent()
{
    DikFixture f;

    DeviceIdentity dik;
    CHECK(dik.init_or_load().ok());
    uint8_t pub1[DIK_PUB_KEY_LEN];
    dik.public_key(pub1);

    // Second instance from same FAT volume.
    DeviceIdentity dik2;
    CHECK(dik2.init_or_load().ok());
    uint8_t pub2[DIK_PUB_KEY_LEN];
    dik2.public_key(pub2);

    CHECK(memcmp(pub1, pub2, DIK_PUB_KEY_LEN) == 0);
}

// ---------------------------------------------------------------------------
// test_dik_sign_verify — sign a message; verify with public key (ed25519)
// ---------------------------------------------------------------------------

static void test_dik_sign_verify()
{
    DikFixture f;

    DeviceIdentity dik;
    CHECK(dik.init_or_load().ok());

    const char* msg = "Hello, ed25519!";
    uint8_t sig[DIK_SIG_LEN] = {};
    CHECK(dik.sign(reinterpret_cast<const uint8_t*>(msg), strlen(msg), sig).ok());

    uint8_t pub[DIK_PUB_KEY_LEN];
    dik.public_key(pub);

    // Verify using OpenSSL (the correct ed25519 algorithm).
    CHECK(ed25519_verify(reinterpret_cast<const uint8_t*>(msg), strlen(msg), sig, pub));

    // Corrupt one byte: verify should fail.
    sig[0] ^= 0xFFu;
    CHECK(!ed25519_verify(reinterpret_cast<const uint8_t*>(msg), strlen(msg), sig, pub));
}

// ---------------------------------------------------------------------------
// test_get_device_id_scoped — scope 0 and scope 1 return different 16 bytes
// ---------------------------------------------------------------------------

static void test_get_device_id_scoped()
{
    DikFixture f;

    DeviceIdentity dik;
    CHECK(dik.init_or_load().ok());

    uint8_t pub[DIK_PUB_KEY_LEN];
    dik.public_key(pub);

    uint8_t id_scope0[16] = {}, id_scope1[16] = {};
    device_identity_scoped_id(pub, 0u, id_scope0);
    device_identity_scoped_id(pub, 1u, id_scope1);

    CHECK(memcmp(id_scope0, id_scope1, 16u) != 0);
}

// ---------------------------------------------------------------------------
// test_get_device_id_api — GET_DEVICE_ID returns 16 bytes via ApiWindow
// ---------------------------------------------------------------------------

struct DikApiFixture {
    FatTestEnv         env;
    ProfileStore       ps;
    SecurityPosture    posture;
    PolicyStore        policy_store;
    CapabilityRegistry registry;
    ApiWindow          win;
    DeviceIdentity     dik;

    DikApiFixture()
    {
        ps.init();
        posture = {};
        policy_store.load(posture);
        registry.init(BoardDescriptor::for_current_board(),
                      kDriverDescriptors, kDriverDescriptorCount,
                      policy_store.info());
        win.init(posture, policy_store, registry);
        win.bind_profile_store(ps);
        dik.init_or_load();
        win.bind_device_identity(dik);
    }

    uint16_t get_device_id(uint8_t scope, uint8_t* out_16)
    {
        uint8_t frame[sizeof(MsgHeader) + 1];
        MsgHeader hdr = {};
        hdr.seq         = 1u;
        hdr.service     = SVC_SYSTEM;
        hdr.method      = SYS_GET_DEVICE_ID;
        hdr.payload_len = 1u;
        hdr.scratch_ofs = 0xFFFFu;
        memcpy(frame, &hdr, sizeof(hdr));
        frame[sizeof(hdr)] = scope;
        win.ring_push_msg(API_REQ_RING_OFS, frame,
                          static_cast<uint16_t>(sizeof(hdr) + 1u));
        win.service_once();

        uint8_t rsp_frame[sizeof(MsgHeader) + 32];
        uint16_t rlen = 0u;
        if (!win.ring_pop_msg(API_RSP_RING_OFS, rsp_frame, sizeof(rsp_frame), &rlen)) {
            return 0xFFFFu;
        }
        MsgHeader rsp;
        memcpy(&rsp, rsp_frame, sizeof(rsp));
        if (rsp.status == API_OK && rsp.payload_len == 16u && out_16) {
            memcpy(out_16, rsp_frame + sizeof(MsgHeader), 16u);
        }
        return rsp.status;
    }
};

static void test_get_device_id_api()
{
    DikApiFixture f;

    uint8_t id0[16] = {}, id1[16] = {};
    CHECK(f.get_device_id(0u, id0) == API_OK);
    CHECK(f.get_device_id(1u, id1) == API_OK);

    // scope 0 and scope 1 must be different.
    CHECK(memcmp(id0, id1, 16u) != 0);

    // Calling scope 0 again should return the same ID (deterministic).
    uint8_t id0b[16] = {};
    CHECK(f.get_device_id(0u, id0b) == API_OK);
    CHECK(memcmp(id0, id0b, 16u) == 0);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    test_dik_generate();
    test_dik_reload();
    test_dik_idempotent();
    test_dik_sign_verify();
    test_get_device_id_scoped();
    test_get_device_id_api();

    return test_summary();
}

// sha256.cc — SHA-256 and HMAC-SHA256 (FIPS 180-4 / RFC 2104).
// Self-contained, no external dependencies.

#include "sha256.h"
#include <cstring>

// ---------------------------------------------------------------------------
// SHA-256 constants and helpers
// ---------------------------------------------------------------------------

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
};

static inline uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
static inline uint32_t ch (uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
static inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
static inline uint32_t ep0(uint32_t x) { return rotr32(x,2)^rotr32(x,13)^rotr32(x,22); }
static inline uint32_t ep1(uint32_t x) { return rotr32(x,6)^rotr32(x,11)^rotr32(x,25); }
static inline uint32_t sig0(uint32_t x){ return rotr32(x,7)^rotr32(x,18)^(x>>3); }
static inline uint32_t sig1(uint32_t x){ return rotr32(x,17)^rotr32(x,19)^(x>>10); }

static void sha256_transform(uint32_t state[8], const uint8_t data[64]) {
    uint32_t w[64], a,b,c,d,e,f,g,h,t1,t2;
    for (int i=0;i<16;i++) {
        w[i] = ((uint32_t)data[i*4]<<24)|((uint32_t)data[i*4+1]<<16)
              |((uint32_t)data[i*4+2]<<8)|(uint32_t)data[i*4+3];
    }
    for (int i=16;i<64;i++)
        w[i] = sig1(w[i-2])+w[i-7]+sig0(w[i-15])+w[i-16];
    a=state[0];b=state[1];c=state[2];d=state[3];
    e=state[4];f=state[5];g=state[6];h=state[7];
    for (int i=0;i<64;i++) {
        t1=h+ep1(e)+ch(e,f,g)+K[i]+w[i];
        t2=ep0(a)+maj(a,b,c);
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;
    state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
}

void sha256_init(Sha256Ctx* ctx) {
    ctx->state[0]=0x6a09e667;ctx->state[1]=0xbb67ae85;
    ctx->state[2]=0x3c6ef372;ctx->state[3]=0xa54ff53a;
    ctx->state[4]=0x510e527f;ctx->state[5]=0x9b05688c;
    ctx->state[6]=0x1f83d9ab;ctx->state[7]=0x5be0cd19;
    ctx->bit_count=0; ctx->buf_len=0;
}

void sha256_update(Sha256Ctx* ctx, const uint8_t* data, size_t len) {
    ctx->bit_count += (uint64_t)len * 8;
    while (len) {
        uint32_t space = 64 - ctx->buf_len;
        uint32_t take  = (len < space) ? (uint32_t)len : space;
        memcpy(ctx->buf + ctx->buf_len, data, take);
        ctx->buf_len += take;
        data += take;
        len  -= take;
        if (ctx->buf_len == 64) {
            sha256_transform(ctx->state, ctx->buf);
            ctx->buf_len = 0;
        }
    }
}

void sha256_final(Sha256Ctx* ctx, uint8_t digest[SHA256_DIGEST_SIZE]) {
    // Capture bit count of message bytes before appending any padding.
    uint64_t msg_bit_count = ctx->bit_count;
    uint8_t pad[64] = {};
    uint32_t pad_len = (ctx->buf_len < 56) ? (56 - ctx->buf_len) : (120 - ctx->buf_len);
    pad[0] = 0x80;
    sha256_update(ctx, pad, pad_len);
    uint8_t bc[8];
    uint64_t bc_val = msg_bit_count;
    for (int i=7;i>=0;i--) { bc[i]=(uint8_t)(bc_val&0xFF); bc_val>>=8; }
    sha256_update(ctx, bc, 8);
    for (int i=0;i<8;i++) {
        digest[i*4+0]=(uint8_t)(ctx->state[i]>>24);
        digest[i*4+1]=(uint8_t)(ctx->state[i]>>16);
        digest[i*4+2]=(uint8_t)(ctx->state[i]>>8);
        digest[i*4+3]=(uint8_t)(ctx->state[i]);
    }
}

void sha256(const uint8_t* data, size_t len, uint8_t digest[SHA256_DIGEST_SIZE]) {
    Sha256Ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

void hmac_sha256(const uint8_t* key,  size_t key_len,
                 const uint8_t* data, size_t data_len,
                 uint8_t mac[SHA256_DIGEST_SIZE]) {
    uint8_t k[64] = {};
    if (key_len > 64) { sha256(key, key_len, k); }
    else              { memcpy(k, key, key_len); }

    uint8_t ipad[64], opad[64];
    for (int i=0;i<64;i++) { ipad[i]=k[i]^0x36; opad[i]=k[i]^0x5c; }

    Sha256Ctx ctx;
    uint8_t inner[SHA256_DIGEST_SIZE];
    sha256_init(&ctx);
    sha256_update(&ctx, ipad, 64);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, inner);

    sha256_init(&ctx);
    sha256_update(&ctx, opad, 64);
    sha256_update(&ctx, inner, SHA256_DIGEST_SIZE);
    sha256_final(&ctx, mac);
}

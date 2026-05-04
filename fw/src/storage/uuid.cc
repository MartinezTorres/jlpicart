// uuid.cc — UUID generation and formatting.

#include "storage/uuid.h"
#include <cstring>

#ifndef JLPICART_HOST_TEST
#include "pico/rand.h"

Uuid Uuid::generate()
{
    Uuid u;
    uint64_t r0 = get_rand_64();
    uint64_t r1 = get_rand_64();
    memcpy(u.bytes,     &r0, 8);
    memcpy(u.bytes + 8, &r1, 8);
    u.bytes[6] = (u.bytes[6] & 0x0Fu) | 0x40u;  // version 4
    u.bytes[8] = (u.bytes[8] & 0x3Fu) | 0x80u;  // variant 1
    return u;
}

#else  // JLPICART_HOST_TEST

static uint32_t g_uuid_seq = 1;

Uuid Uuid::generate()
{
    Uuid u;
    memset(u.bytes, 0, 16);
    uint32_t seq = g_uuid_seq++;
    memcpy(u.bytes, &seq, 4);
    u.bytes[6] = 0x40u;
    u.bytes[8] = 0x80u;
    return u;
}

#endif  // JLPICART_HOST_TEST

void Uuid::to_hex(char* buf) const
{
    static const char h[] = "0123456789abcdef";
    for (int i = 0; i < 16; ++i) {
        buf[i * 2]     = h[bytes[i] >> 4];
        buf[i * 2 + 1] = h[bytes[i] & 0x0Fu];
    }
    buf[32] = '\0';
}

static int nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool Uuid::from_hex(const char* hex, Uuid& out)
{
    for (int i = 0; i < 16; ++i) {
        int hi = nibble(hex[i * 2]);
        int lo = nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out.bytes[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

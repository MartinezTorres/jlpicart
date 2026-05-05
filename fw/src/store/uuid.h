#pragma once
// uuid.h — 128-bit UUID (version 4, random).

#include <cstdint>
#include <cstddef>
#include <cstring>

struct Uuid {
    uint8_t bytes[16];

    bool operator==(const Uuid& o) const { return memcmp(bytes, o.bytes, 16) == 0; }
    bool operator!=(const Uuid& o) const { return !(*this == o); }

    bool is_zero() const {
        for (int i = 0; i < 16; ++i) if (bytes[i]) return false;
        return true;
    }

    // Generate a random version-4 UUID.
    static Uuid generate();

    static Uuid zero() { Uuid u; memset(u.bytes, 0, 16); return u; }

    // Write 32 lowercase hex chars + null terminator.  buf must be >= 33 bytes.
    void to_hex(char* buf) const;

    // Parse 32 hex chars (no hyphens).  Returns false on malformed input.
    static bool from_hex(const char* hex, Uuid& out);
};

static_assert(sizeof(Uuid) == 16, "Uuid must be 16 bytes");

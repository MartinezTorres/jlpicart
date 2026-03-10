// manifest_parser.cc — Minimal strict JSON parser for Collection manifests.

#include "content/manifest_parser.h"
#include <cstring>
#include <cctype>

// ---------------------------------------------------------------------------
// JSON tokenizer
// ---------------------------------------------------------------------------

enum class Tok : uint8_t {
    LBRACE,   // {
    RBRACE,   // }
    LBRACKET, // [
    RBRACKET, // ]
    COLON,    // :
    COMMA,    // ,
    STR,      // "..." — value in sv[0..sv_len-1], NUL-terminated
    NUM,      // number (value discarded)
    BOOL_,    // true or false (value discarded)
    NULL_,    // null
    END,      // end of input
    ERR       // parse error
};

struct Scanner {
    const char* src;
    size_t      len;
    size_t      pos;
    char        sv[512];  // current string value, NUL-terminated
    size_t      sv_len;

    void skip_ws() {
        while (pos < len) {
            char c = src[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos;
            else break;
        }
    }

    Tok next() {
        skip_ws();
        if (pos >= len) return Tok::END;
        char c = src[pos++];
        switch (c) {
            case '{': return Tok::LBRACE;
            case '}': return Tok::RBRACE;
            case '[': return Tok::LBRACKET;
            case ']': return Tok::RBRACKET;
            case ':': return Tok::COLON;
            case ',': return Tok::COMMA;
            case '"': {
                sv_len = 0;
                while (pos < len) {
                    char ch = src[pos++];
                    if (ch == '"') {
                        if (sv_len < sizeof(sv)) sv[sv_len] = '\0';
                        return Tok::STR;
                    }
                    if (ch == '\\') {
                        if (pos >= len) return Tok::ERR;
                        char esc = src[pos++];
                        switch (esc) {
                            case '"': case '\\': case '/': ch = esc; break;
                            case 'b': ch = '\b'; break;
                            case 'f': ch = '\f'; break;
                            case 'n': ch = '\n'; break;
                            case 'r': ch = '\r'; break;
                            case 't': ch = '\t'; break;
                            default: return Tok::ERR; // \uXXXX not supported
                        }
                    } else if ((unsigned char)ch < 0x20u) {
                        return Tok::ERR; // unescaped control character
                    }
                    if (sv_len < sizeof(sv) - 1u) sv[sv_len++] = ch;
                    else return Tok::ERR; // string too long for our buffer
                }
                return Tok::ERR; // unterminated string
            }
            case 't': {
                if (pos + 3 <= len && memcmp(src + pos, "rue", 3) == 0) {
                    pos += 3; return Tok::BOOL_;
                }
                return Tok::ERR;
            }
            case 'f': {
                if (pos + 4 <= len && memcmp(src + pos, "alse", 4) == 0) {
                    pos += 4; return Tok::BOOL_;
                }
                return Tok::ERR;
            }
            case 'n': {
                if (pos + 3 <= len && memcmp(src + pos, "ull", 3) == 0) {
                    pos += 3; return Tok::NULL_;
                }
                return Tok::ERR;
            }
            default: {
                // Number: leading digit or minus.
                if (c == '-' || (c >= '0' && c <= '9')) {
                    while (pos < len) {
                        char nc = src[pos];
                        if ((nc >= '0' && nc <= '9') || nc == '.' ||
                            nc == 'e' || nc == 'E' || nc == '+' || nc == '-')
                            ++pos;
                        else break;
                    }
                    return Tok::NUM;
                }
                return Tok::ERR;
            }
        }
    }

    // Copy sv[] → dst (at most max-1 chars + NUL).  Returns false if too long.
    bool copy_sv(char* dst, size_t max) const {
        if (sv_len >= max) return false;
        memcpy(dst, sv, sv_len);
        dst[sv_len] = '\0';
        return true;
    }
};

// Skip one complete JSON value whose first token has already been consumed.
static bool skip_value(Scanner& s, Tok first) {
    if (first == Tok::LBRACE) {
        int depth = 1;
        while (depth > 0) {
            Tok t = s.next();
            if (t == Tok::ERR || t == Tok::END) return false;
            if (t == Tok::LBRACE)  ++depth;
            if (t == Tok::RBRACE)  --depth;
        }
        return true;
    }
    if (first == Tok::LBRACKET) {
        int depth = 1;
        while (depth > 0) {
            Tok t = s.next();
            if (t == Tok::ERR || t == Tok::END) return false;
            if (t == Tok::LBRACKET) ++depth;
            if (t == Tok::RBRACKET) --depth;
        }
        return true;
    }
    // Primitives (STR, NUM, BOOL_, NULL_) already consumed.
    return (first == Tok::STR || first == Tok::NUM ||
            first == Tok::BOOL_ || first == Tok::NULL_);
}

static const DiagStatus kBadManifest =
    DiagStatus::error(DiagCode::COLLECTION_BAD_MANIFEST);

// ---------------------------------------------------------------------------
// Hex / base64 helpers
// ---------------------------------------------------------------------------

static uint8_t hex_nibble(char c) {
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
    return 0xFFu; // invalid
}

// Decode exactly 64 hex chars into 32 bytes.  Returns false on bad input.
static bool hex_decode_32(const char* hex, size_t hex_len, uint8_t out[32]) {
    if (hex_len != 64u) return false;
    for (int i = 0; i < 32; ++i) {
        uint8_t hi = hex_nibble(hex[2 * i]);
        uint8_t lo = hex_nibble(hex[2 * i + 1]);
        if (hi == 0xFFu || lo == 0xFFu) return false;
        out[i] = static_cast<uint8_t>((hi << 4u) | lo);
    }
    return true;
}

static int8_t b64val(char c) {
    if (c >= 'A' && c <= 'Z') return static_cast<int8_t>(c - 'A');
    if (c >= 'a' && c <= 'z') return static_cast<int8_t>(c - 'a' + 26);
    if (c >= '0' && c <= '9') return static_cast<int8_t>(c - '0' + 52);
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1; // '=' padding or invalid
}

// Decode base64 string → bytes.  Returns decoded byte count or -1 on error.
static int base64_decode(const char* src, size_t src_len,
                          uint8_t* dst, size_t dst_max) {
    size_t out = 0;
    size_t i   = 0;
    while (i + 3 < src_len) {
        // Skip whitespace between groups (MIME line breaks).
        while (i < src_len && (src[i] == '\n' || src[i] == '\r' || src[i] == ' '))
            ++i;
        if (i + 3 >= src_len) break;
        int8_t a = b64val(src[i]);
        int8_t b = b64val(src[i + 1]);
        int8_t c = b64val(src[i + 2]);
        int8_t d = b64val(src[i + 3]);
        if (a < 0 || b < 0) return -1;
        if (out >= dst_max) return -1;
        dst[out++] = static_cast<uint8_t>((a << 2) | (b >> 4));
        if (src[i + 2] != '=' && c >= 0) {
            if (out >= dst_max) return -1;
            dst[out++] = static_cast<uint8_t>(((b & 0x0F) << 4) | (c >> 2));
        }
        if (src[i + 3] != '=' && d >= 0) {
            if (out >= dst_max) return -1;
            dst[out++] = static_cast<uint8_t>(((c & 0x03) << 6) | d);
        }
        i += 4;
    }
    return static_cast<int>(out);
}

// ---------------------------------------------------------------------------
// Object-body parsing helpers (LBRACE already consumed by caller)
// ---------------------------------------------------------------------------

// Parse the publisher object body and write into manifest.
static DiagStatus parse_publisher_body(Scanner& s, CollectionManifest& m) {
    Tok t = s.next();
    while (t != Tok::RBRACE) {
        if (t != Tok::STR) return kBadManifest;
        char key[64];
        if (!s.copy_sv(key, sizeof(key))) return kBadManifest;
        if (s.next() != Tok::COLON) return kBadManifest;
        Tok vt = s.next();
        if (strcmp(key, "publisher_id") == 0) {
            if (vt != Tok::STR) return kBadManifest;
            if (!s.copy_sv(m.publisher_id, sizeof(m.publisher_id))) return kBadManifest;
        } else if (strcmp(key, "name") == 0) {
            if (vt != Tok::STR) return kBadManifest;
            if (!s.copy_sv(m.publisher_name, sizeof(m.publisher_name))) return kBadManifest;
        } else {
            if (!skip_value(s, vt)) return kBadManifest;
        }
        t = s.next();
        if (t == Tok::COMMA) t = s.next();
        else if (t != Tok::RBRACE) return kBadManifest;
    }
    return DiagStatus::success();
}

// Parse the boot object body.
static DiagStatus parse_boot_body(Scanner& s, CollectionManifest& m) {
    Tok t = s.next();
    while (t != Tok::RBRACE) {
        if (t != Tok::STR) return kBadManifest;
        char key[64];
        if (!s.copy_sv(key, sizeof(key))) return kBadManifest;
        if (s.next() != Tok::COLON) return kBadManifest;
        Tok vt = s.next();
        if (strcmp(key, "mode") == 0) {
            if (vt != Tok::STR) return kBadManifest;
            if (strcmp(s.sv, "direct") == 0)      m.boot_mode = 1u;
            else if (strcmp(s.sv, "menu_first") == 0) m.boot_mode = 0u;
            else return kBadManifest; // unknown boot mode
        } else if (strcmp(key, "payload_id") == 0) {
            if (vt != Tok::STR) return kBadManifest;
            if (!s.copy_sv(m.default_payload_id, sizeof(m.default_payload_id)))
                return kBadManifest;
        } else {
            if (!skip_value(s, vt)) return kBadManifest;
        }
        t = s.next();
        if (t == Tok::COMMA) t = s.next();
        else if (t != Tok::RBRACE) return kBadManifest;
    }
    // direct boot requires a target payload (spec §6.3.1).
    if (m.boot_mode == 1u && m.default_payload_id[0] == '\0') return kBadManifest;
    return DiagStatus::success();
}

// Parse one payload object body (LBRACE already consumed).
static DiagStatus parse_payload_body(Scanner& s, PayloadEntry& pe) {
    Tok t = s.next();
    while (t != Tok::RBRACE) {
        if (t != Tok::STR) return kBadManifest;
        char key[64];
        if (!s.copy_sv(key, sizeof(key))) return kBadManifest;
        if (s.next() != Tok::COLON) return kBadManifest;
        Tok vt = s.next();
        if (strcmp(key, "payload_id") == 0) {
            if (vt != Tok::STR) return kBadManifest;
            if (!s.copy_sv(pe.payload_id, sizeof(pe.payload_id))) return kBadManifest;
        } else if (strcmp(key, "path") == 0) {
            if (vt != Tok::STR) return kBadManifest;
            if (!s.copy_sv(pe.path, sizeof(pe.path))) return kBadManifest;
        } else if (strcmp(key, "title") == 0) {
            if (vt != Tok::STR) return kBadManifest;
            if (!s.copy_sv(pe.title, sizeof(pe.title))) return kBadManifest;
        } else {
            if (!skip_value(s, vt)) return kBadManifest;
        }
        t = s.next();
        if (t == Tok::COMMA) t = s.next();
        else if (t != Tok::RBRACE) return kBadManifest;
    }
    if (pe.payload_id[0] == '\0') return kBadManifest; // payload_id required
    if (pe.path[0] == '\0')       return kBadManifest; // path required
    return DiagStatus::success();
}

// Parse the payloads array (LBRACKET already consumed).
static DiagStatus parse_payloads_array(Scanner& s, CollectionManifest& m) {
    Tok t = s.next();
    while (t != Tok::RBRACKET) {
        if (t != Tok::LBRACE) return kBadManifest;
        if (m.payload_count >= MANIFEST_MAX_PAYLOADS) {
            // Silently skip payloads beyond our limit.
            if (!skip_value(s, Tok::LBRACE)) return kBadManifest;
        } else {
            PayloadEntry& pe = m.payloads[m.payload_count];
            memset(&pe, 0, sizeof(pe));
            DiagStatus ds = parse_payload_body(s, pe);
            if (!ds.ok()) return ds;
            ++m.payload_count;
        }
        t = s.next();
        if (t == Tok::COMMA) t = s.next();
        else if (t != Tok::RBRACKET) return kBadManifest;
    }
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// parse_collection_manifest
// ---------------------------------------------------------------------------

DiagStatus parse_collection_manifest(const char* json, size_t len,
                                      CollectionManifest& out)
{
    memset(&out, 0, sizeof(out));
    Scanner s{json, len, 0u, {}, 0u};

    if (s.next() != Tok::LBRACE) return kBadManifest;

    bool saw_format_version = false;
    bool saw_collection_id  = false;
    bool saw_version        = false;
    bool saw_payloads       = false;

    Tok t = s.next();
    while (t != Tok::RBRACE) {
        if (t != Tok::STR) return kBadManifest;
        char key[64];
        if (!s.copy_sv(key, sizeof(key))) return kBadManifest;
        if (s.next() != Tok::COLON) return kBadManifest;
        Tok vt = s.next();

        if (strcmp(key, "format_version") == 0) {
            if (vt != Tok::STR) return kBadManifest;
            if (strcmp(s.sv, "1.0") != 0) return kBadManifest;
            saw_format_version = true;
        } else if (strcmp(key, "collection_id") == 0) {
            if (vt != Tok::STR || s.sv_len == 0) return kBadManifest;
            if (!s.copy_sv(out.collection_id, sizeof(out.collection_id))) return kBadManifest;
            saw_collection_id = true;
        } else if (strcmp(key, "version") == 0) {
            if (vt != Tok::STR || s.sv_len == 0) return kBadManifest;
            if (!s.copy_sv(out.version, sizeof(out.version))) return kBadManifest;
            saw_version = true;
        } else if (strcmp(key, "schema") == 0) {
            if (vt != Tok::STR) return kBadManifest;
            if (!s.copy_sv(out.schema, sizeof(out.schema))) return kBadManifest;
        } else if (strcmp(key, "publisher") == 0) {
            if (vt != Tok::LBRACE) return kBadManifest;
            DiagStatus ds = parse_publisher_body(s, out);
            if (!ds.ok()) return ds;
        } else if (strcmp(key, "title") == 0) {
            if (vt != Tok::STR) return kBadManifest;
            if (!s.copy_sv(out.title, sizeof(out.title))) return kBadManifest;
        } else if (strcmp(key, "boot") == 0) {
            if (vt != Tok::LBRACE) return kBadManifest;
            DiagStatus ds = parse_boot_body(s, out);
            if (!ds.ok()) return ds;
        } else if (strcmp(key, "payloads") == 0) {
            if (vt != Tok::LBRACKET) return kBadManifest;
            DiagStatus ds = parse_payloads_array(s, out);
            if (!ds.ok()) return ds;
            saw_payloads = true;
        } else {
            if (!skip_value(s, vt)) return kBadManifest;
        }

        t = s.next();
        if (t == Tok::COMMA) t = s.next();
        else if (t != Tok::RBRACE) return kBadManifest;
    }

    // Validate required fields (spec §6.3.1).
    if (!saw_format_version) return kBadManifest;
    if (!saw_collection_id)  return kBadManifest;
    if (!saw_version)        return kBadManifest;
    if (!saw_payloads || out.payload_count == 0) return kBadManifest;

    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// parse_bundle_sig helpers
// ---------------------------------------------------------------------------

static DiagStatus parse_file_entry_body(Scanner& s, BundleFileHash& fh) {
    Tok t = s.next();
    while (t != Tok::RBRACE) {
        if (t != Tok::STR) return kBadManifest;
        char key[64];
        if (!s.copy_sv(key, sizeof(key))) return kBadManifest;
        if (s.next() != Tok::COLON) return kBadManifest;
        Tok vt = s.next();
        if (strcmp(key, "path") == 0) {
            if (vt != Tok::STR) return kBadManifest;
            if (!s.copy_sv(fh.path, sizeof(fh.path))) return kBadManifest;
        } else if (strcmp(key, "sha256") == 0) {
            if (vt != Tok::STR) return kBadManifest;
            if (!hex_decode_32(s.sv, s.sv_len, fh.sha256)) return kBadManifest;
        } else {
            if (!skip_value(s, vt)) return kBadManifest;
        }
        t = s.next();
        if (t == Tok::COMMA) t = s.next();
        else if (t != Tok::RBRACE) return kBadManifest;
    }
    if (fh.path[0] == '\0') return kBadManifest;
    return DiagStatus::success();
}

static DiagStatus parse_files_array(Scanner& s, BundleSigEnvelope& env) {
    Tok t = s.next();
    while (t != Tok::RBRACKET) {
        if (t != Tok::LBRACE) return kBadManifest;
        if (env.file_count >= BUNDLE_MAX_FILES) {
            if (!skip_value(s, Tok::LBRACE)) return kBadManifest;
        } else {
            BundleFileHash& fh = env.files[env.file_count];
            memset(&fh, 0, sizeof(fh));
            DiagStatus ds = parse_file_entry_body(s, fh);
            if (!ds.ok()) return ds;
            ++env.file_count;
        }
        t = s.next();
        if (t == Tok::COMMA) t = s.next();
        else if (t != Tok::RBRACKET) return kBadManifest;
    }
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// parse_bundle_sig
// ---------------------------------------------------------------------------

DiagStatus parse_bundle_sig(const char* json, size_t len,
                             BundleSigEnvelope& out)
{
    memset(&out, 0, sizeof(out));
    out.has_envelope = true;
    Scanner s{json, len, 0u, {}, 0u};

    if (s.next() != Tok::LBRACE) return kBadManifest;

    Tok t = s.next();
    while (t != Tok::RBRACE) {
        if (t != Tok::STR) return kBadManifest;
        char key[64];
        if (!s.copy_sv(key, sizeof(key))) return kBadManifest;
        if (s.next() != Tok::COLON) return kBadManifest;
        Tok vt = s.next();

        if (strcmp(key, "sig_schema") == 0) {
            if (vt != Tok::STR) return kBadManifest;
            if (!s.copy_sv(out.sig_schema, sizeof(out.sig_schema))) return kBadManifest;
        } else if (strcmp(key, "alg") == 0) {
            if (vt != Tok::STR) return kBadManifest;
            if (!s.copy_sv(out.alg, sizeof(out.alg))) return kBadManifest;
        } else if (strcmp(key, "key_id") == 0) {
            if (vt != Tok::STR) return kBadManifest;
            if (!s.copy_sv(out.key_id, sizeof(out.key_id))) return kBadManifest;
        } else if (strcmp(key, "files") == 0) {
            if (vt != Tok::LBRACKET) return kBadManifest;
            DiagStatus ds = parse_files_array(s, out);
            if (!ds.ok()) return ds;
        } else if (strcmp(key, "signature") == 0) {
            if (vt != Tok::STR) return kBadManifest;
            int decoded = base64_decode(s.sv, s.sv_len,
                                         out.signature, sizeof(out.signature));
            if (decoded < 0) return kBadManifest;
            out.sig_len = static_cast<uint8_t>(decoded);
        } else {
            if (!skip_value(s, vt)) return kBadManifest;
        }

        t = s.next();
        if (t == Tok::COMMA) t = s.next();
        else if (t != Tok::RBRACE) return kBadManifest;
    }

    return DiagStatus::success();
}

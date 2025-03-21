
namespace  CartridgeConfig {

    uint32_t parse_dec(const char *s) {
        uint32_t t = 0;
        while (*s>='0' and *s<='9') t = t * 10 + *s++ - '0';
        return t;
    }

    uint32_t parse_hex(const char *s) {
        uint32_t t = 0;
        while (true) {
                 if (*s>='0' and *s<='9') t = t * 16 + *s++ - '0';
            else if (*s>='A' and *s<='F') t = t * 16 + *s++ - 'A' + 10;
            else if (*s>='a' and *s<='f') t = t * 16 + *s++ - 'a' + 10;
            else return t;
        }
    }

    uint32_t parse_unsigned(const char *s) {
        if (s[0]=='0' and s[1]=='x') return parse_hex(s);
        return parse_dec(s);
    }

    int32_t parse_signed(const char *s) {
        if (s[0]=='-') return -int32_t(parse_unsigned(s+1));
        return int32_t(parse_unsigned(s));
    }

    std::pair<const char *, const char *> parse_string(const char *s) {
        
        const char *s_end = s + 1;
        const char end_token = ( *s == '"' ? '"' : ' '); 
        while (*s_end and *s_end != end_token) s_end++;
        return {s, s_end};
    }

    const char *parse_key(const char *key, const char *config) {

        for (const char *c = config; *c; c++) {

            const char *k = key;
            while (*k and *k == *c) { k++; c++;}
            if (*k == 0 and *c == '=') return c+1;
        }
        return nullptr;
    }
}
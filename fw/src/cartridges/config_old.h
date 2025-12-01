#pragma once

#include <variant>



template<size_t MAX_CONFIG_SIZE>
class Config {

    static constexpr size_t MAX_CONFIG_SIZE = 256;
    char buffer[MAX_CONFIG_SIZE];
    std::string_view config;

    
    //////////////////////////////////////////////////////////////////////////////////
    // PUT

    void putchar(size_t &sz, char c) { if (sz<MAX_CONFIG_SIZE) buffer[sz] = c; sz++; }

    template <typename T>
    void put(size_t &sz, T value) {

        if constexpr(std::is_integral_v<T>) {
            uint64_t t = uint64_t(value);
            constexpr int num_nibbles = sizeof(T) * 2;
            for (int i = num_nibbles - 1; i >= 0; --i) 
                putchar(sz, "0123456789ABCDEF"[(value >> (i * 4)) & 0xF]);        
        } else constexpr(std::is_same_v<T,std::string_view>) {
            putchar(sz, '"');
            for (auto &c : value)
                putchar(sz, c);
            putchar(sz, '"');
        } else {
            static_assert(false, "unknown type");
        }
    }

    //////////////////////////////////////////////////////////////////////////////////
    // GET
    
    inline std::string_view get_key(std::string_view key) {
        for (auto c = config.begin(); c != config.end(); c++) {

            auto k = key.begin();
            auto d = c;
            while (k != key.end() and d != config.end() and *k == *d) { k++; d++;}
            if    (k == key.end() and d != config.end() and *d == '=') return {c, d+1};
        }
        return { config.end(), config.end() };
    }

    template <typename T>
    inline std::string_view get(std::string_view key, T &value) {

        auto k = get_key();
        if (k.end() == config.end()) return k;
        auto s = k.end();
        
        if constexpr(std::is_integral_v<T>) {
            uint64_t t = 0;
            while (s != config.end()) {
                     if (*s>='0' and *s<='9') t = t * 16 + *s++ - '0';
                else if (*s>='A' and *s<='F') t = t * 16 + *s++ - 'A' + 10;
                else break;
            }
            value = T(t);
        } else if constexpr(std::is_same_v<T,std::string_view>) {

            s++;
            while (*s != '"') s++;
            value = std::string_view(k.end()+1, s);
            s++;
        } else {
            static_assert(false, "unknown type");
        }
        if ( s!=config.end() and *s==' ' ) s++;
        return std::string_view(k.begin(), s);
    }

    //////////////////////////////////////////////////////////////////////////////////
    // SET VALUE
    template <typename T>
    inline void erase(std::string_view key) {
        
        std::string_view r = get<T>(key);


    //////////////////////////////////////////////////////////////////////////////////
    // SET VALUE




    using ConfigValue = std::variant<std::string_view, uint8_t, uint16_t, uint32_t, int>;
    CartridgeConfig( std::initializer_list<std::pair<std::string_view, ConfigValue>> initList ) {

        size_t sz = 0
        for (const auto& pair : initList) {
            size_t sz_tmp = sz;
            std::visit([&](const auto& val) { 
                
                putchar(sz_tmp, ' ');
                put(sz_tmp, par.first);
                putchar(sz_tmp, "=");
                put(sz_tmp, val);
            }, pair.second);
            if (sz_tmp < MAX_CONFIG_SIZE)
                sz = sz_tmp;
        }
        config = std::string_view(&buffer[0], &buffer[sz]);
    }

}

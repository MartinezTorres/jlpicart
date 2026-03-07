#pragma once

struct {
    const char *name;
    const char *configuration = "";
    Cartridge::
};



struct CartridgeRegister {
    static Cartridge Disabled() { return Cartridge(); }
    static Cartridge RAM(uint8_t *ram_base ) { return Cartridge { .name = "RAM", .ram_base = ram_base }; }
}




void init( uint8_t *&heap, const char *config_ ) {

    {
        auto *pcart = cartridge_register.find(get_string("cart"));
        if (pcart and pcart->deinit) pcart->deinit(*this);
    }

    *this = Cartridge();
    config = config_;

    {
        auto *pcart = cartridge_register.find(get_string("cart"));
        if (pcart and pcart->init) pcart->init(heap, *this);
    }
}
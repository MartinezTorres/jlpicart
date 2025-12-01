#include <board.h>


#include "generated_roms/roms.list.h"

void on_reset() {

    static int n_cart = 0;

    DBG::msg<DEBUG_INFO>("Reset routine started");

    // Load persisted configuration from flash before applying defaults for this boot.
    config.load();

    ///////////////////////////////////////////////////////////////////////////
    // DISABLE ALL EXISTING CARTRIDGES
    BUS::remove_all_cartridges();

    Multitask::clear_tasks();

    ///////////////////////////////////////////////////////////////////////////
    // INIT BUS AND SUBSLOTS
    
    auto &rom = roms_in_flash_list[n_cart];
    config["SUBSLOT0"] = { {"class", "ROM"}, {"name", rom.name}, {"mapper", rom.mapper}, {"address", rom.address}, {"size", rom.size} };
    n_cart++;
    if (n_cart == sizeof( roms_in_flash_list) / sizeof (roms_in_flash_list[0]) ) n_cart = 0;

    BUS::insert_all_cartridges();

    DBG::msg<DEBUG_INFO>("Reset routine finished");
}

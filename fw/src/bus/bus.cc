// bus.cc — MSX bus loop for jlpicart_board (RP2350 Core 0).
//
// Hardware-only: this file is excluded from host-test builds.
// The guard below is a belt-and-suspenders check; the CMakeLists.txt
// already omits this file from test_bus_mapper.
//
// GPIO constants come from boards/gpio_defs.h; Pico SDK hardware-register
// headers are the only SDK dependency here.

#ifndef JLPICART_HOST_TEST

#include "bus/bus.h"
#include "boards/gpio_defs.h"

#include <hardware/structs/sio.h>
#include <hardware/structs/systick.h>
#include <hardware/sync.h>

namespace BUS {

    // -----------------------------------------------------------------------
    // Bus helper lambdas and the main loop live in a single function so the
    // compiler can keep the hot state in registers as much as possible.
    // (Global state defined in bus_state.cc — compiled for all targets.)
    // -----------------------------------------------------------------------

    [[noreturn]] void __no_inline_not_in_flash_func(start)() {

        auto get_bus    = []() { return sio_hw->gpio_in; };
        auto get_bus_hi = []() { return sio_hw->gpio_hi_in; };
        auto set_bus    = [](uint32_t v) { sio_hw->gpio_out = v; };
        auto set_bus_oe = [](uint32_t v) { sio_hw->gpio_oe  = v; };

        save_and_disable_interrupts();

        // Assert WAIT before signalling ready so the MSX cannot start a cycle
        // while we are still initialising.
        set_bus_oe(BIT_WAIT);
        if (reset_callback) reset_callback();
        set_bus_oe(0);

        // Wait for RESET to deassert before entering the normal bus loop.
        while ((get_bus_hi() & BIT64_RESET_HI) == 0) { /* spin */ }

        // -----------------------------------------------------------------------
        // Bus signal masks for cycle-type detection (active-low signals).
        // -----------------------------------------------------------------------
        constexpr uint32_t memory_read_mask  = BIT_MERQ | BIT_RD           | BIT_SLTSL;
        constexpr uint32_t memory_write_mask = BIT_MERQ |          BIT_WR  | BIT_SLTSL;
        constexpr uint32_t io_read_mask      =            BIT_IORQ | BIT_RD;
        constexpr uint32_t io_write_mask     =            BIT_IORQ |          BIT_WR;

        uint32_t tick_start = systick_hw->cvr;
        uint32_t bus_prev   = 0xFFFF'FFFFu;
        uint32_t bus        = get_bus();

        // -----------------------------------------------------------------------
        // Subslot register union (read as bitfields, written as byte).
        // -----------------------------------------------------------------------
        union SubslotReg {
            uint8_t byte;
            struct { uint8_t p0 : 2; uint8_t p1 : 2; uint8_t p2 : 2; uint8_t p3 : 2; } pages;
        };

        // -----------------------------------------------------------------------
        // memory_read — drive D[7:0] before releasing WAIT.
        // -----------------------------------------------------------------------
        auto memory_read = [&]() {
            set_bus_oe(BIT_WAIT);

            uint32_t address     = (bus >> GPIO_A0)  & 0xFFFFu;
            uint32_t segment8k   = (bus >> GPIO_A13) & 0x07u;
            uint32_t displacement= (bus >> GPIO_A0)  & 0x1FFFu;
            uint32_t page        = (bus >> GPIO_A14) & 0x03u;

            Subslot& cart = subslots[is_expanded ? subslot_indexes[page] : 0];

            uint32_t data = 0;
            if (is_expanded && address == 0xFFFFu) {
                // Read back the complement of the subslot register.
                SubslotReg r;
                r.pages.p0 = subslot_indexes[0]; r.pages.p1 = subslot_indexes[1];
                r.pages.p2 = subslot_indexes[2]; r.pages.p3 = subslot_indexes[3];
                data = (~r.byte) & 0xFFu;
            } else {
                const uint8_t* seg_ptr = cart.memory_read_addresses[segment8k];
                if (seg_ptr) data = seg_ptr[displacement];

                auto cb = cart.memory_read_callbacks[segment8k];
                if (cb) {
                    auto [active, d] = cb(cart, bus);
                    if (active) data = d;
                }
            }

            set_bus(data << GPIO_D0);
            set_bus_oe((0xFFu << GPIO_D0) | BIT_BUSDIR);
            while ((get_bus() & BIT_RD) == 0) { /* hold until /RD deasserts */ }
            set_bus_oe(BIT_WAIT);
        };

        // -----------------------------------------------------------------------
        // memory_write — latch data then release WAIT.
        // -----------------------------------------------------------------------
        auto memory_write = [&]() {
            set_bus_oe(BIT_WAIT);

            uint32_t address     = (bus >> GPIO_A0)  & 0xFFFFu;
            uint32_t segment8k   = (bus >> GPIO_A13) & 0x07u;
            uint32_t displacement= (bus >> GPIO_A0)  & 0x1FFFu;
            uint32_t page        = (bus >> GPIO_A14) & 0x03u;
            uint32_t data        = (bus >> GPIO_D0)  & 0xFFu;

            Subslot& cart = subslots[is_expanded ? subslot_indexes[page] : 0];

            if (is_expanded && address == 0xFFFFu) {
                SubslotReg r; r.byte = static_cast<uint8_t>(data);
                subslot_indexes[0] = r.pages.p0; subslot_indexes[1] = r.pages.p1;
                subslot_indexes[2] = r.pages.p2; subslot_indexes[3] = r.pages.p3;
            } else {
                auto cb = cart.memory_write_callbacks[segment8k];
                if (cb) cb(cart, bus);

                uint8_t* seg_ptr = cart.memory_write_addresses[segment8k];
                if (seg_ptr) seg_ptr[displacement] = static_cast<uint8_t>(data);
            }

            while ((get_bus() & BIT_WR) == 0) { /* hold until /WR deasserts */ }
        };

        // -----------------------------------------------------------------------
        // io_read — all cartridge slots polled; first active response wins.
        // -----------------------------------------------------------------------
        auto io_read = [&]() {
            set_bus_oe(BIT_WAIT);

            uint32_t io_port = (bus >> GPIO_A0) & 0xFFu;

            bool    driven = false;
            uint8_t data   = 0;
            for (size_t i = 0; i < SUBSLOT_COUNT; ++i) {
                auto cb = subslots[i].io_read_callbacks[io_port];
                if (cb) {
                    auto [active, d] = cb(subslots[i], bus);
                    if (active) { driven = true; data = d; }
                }
            }

            if (driven) {
                set_bus(static_cast<uint32_t>(data) << GPIO_D0);
                set_bus_oe((0xFFu << GPIO_D0) | BIT_BUSDIR);
            } else {
                set_bus_oe(0);
            }

            while ((get_bus() & BIT_RD) == 0) { /* hold until /RD deasserts */ }
            set_bus_oe(0);
        };

        // -----------------------------------------------------------------------
        // io_write — all cartridge slots notified.
        // -----------------------------------------------------------------------
        auto io_write = [&]() {
            set_bus_oe(BIT_WAIT);

            uint32_t io_port = (bus >> GPIO_A0) & 0xFFu;
            for (size_t i = 0; i < SUBSLOT_COUNT; ++i) {
                auto cb = subslots[i].io_write_callbacks[io_port];
                if (cb) cb(subslots[i], bus);
            }

            set_bus_oe(0);
            while ((get_bus() & BIT_WR) == 0) { /* hold until /WR deasserts */ }
        };

        // -----------------------------------------------------------------------
        // Idle handlers: IRQ edge detection, RESET polling.
        // -----------------------------------------------------------------------
        auto irq_request = [&]() {
            // Record falling edge of /INT (MSX IRQ) for timing diagnostics.
            if ((bus_prev & BIT_INT) && !(bus & BIT_INT)) tick_start = systick_hw->cvr;
            bus_prev = bus;
        };

        auto reset_request = [&]() {
            if ((get_bus_hi() & BIT64_RESET_HI) == 0) {
                set_bus_oe(BIT_WAIT);
                if (reset_callback) reset_callback();
                set_bus_oe(0);
                while ((get_bus_hi() & BIT64_RESET_HI) == 0) { /* spin */ }
            }
        };

        // -----------------------------------------------------------------------
        // Main bus loop — runs forever.
        // -----------------------------------------------------------------------
        while (true) {
            tick_start = systick_hw->cvr;
            bus = get_bus();

            if      ((bus & memory_read_mask)  == 0) { memory_read();  }
            else if ((bus & memory_write_mask) == 0) { memory_write(); }
            else if ((bus & io_read_mask)      == 0) { io_read();      }
            else if ((bus & io_write_mask)     == 0) { io_write();     }
            else {
                reset_request();
                irq_request();
                set_bus_oe(0);
            }
        }
    }

}  // namespace BUS

#endif  // !JLPICART_HOST_TEST

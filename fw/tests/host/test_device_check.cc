// test_device_check.cc — Tests for check_device_compatibility()

#include "spine/device_check.h"
#include "peripherals/peripheral_descriptor.h"
#include <cassert>
#include <cstdio>

// Budget large enough to never fail on SRAM alone in these tests.
static constexpr uint32_t BIG_BUDGET = 512u * 1024u;

static void test_empty() {
    DeviceCheckResult r = check_device_compatibility(nullptr, 0, BIG_BUDGET);
    assert(r.ok);
    assert(r.conflict == DeviceConflict::NONE);
    printf("PASS: test_empty\n");
}

static void test_single_psg() {
    DeviceCheckEntry d[] = {{ find_peripheral_by_name("psg"),         0 }};
    DeviceCheckResult r = check_device_compatibility(d, 1, BIG_BUDGET);
    assert(r.ok);
    printf("PASS: test_single_psg\n");
}

static void test_psg_opl4_compatible() {
    // PSG (0xA0–0xA2) and OPL4 (0x7C–0x7F) have no port overlap.
    DeviceCheckEntry d[] = {
        { find_peripheral_by_name("psg"),         0 },
        { find_peripheral_by_name("opl4"),        0 },
    };
    DeviceCheckResult r = check_device_compatibility(d, 2, BIG_BUDGET);
    assert(r.ok);
    printf("PASS: test_psg_opl4_compatible\n");
}

static void test_two_psgs_io_conflict() {
    // Two PSGs share IO 0xA0–0xA2 — conflict.
    DeviceCheckEntry d[] = {
        { find_peripheral_by_name("psg"),         0 },
        { find_peripheral_by_name("psg"),         0 },
    };
    DeviceCheckResult r = check_device_compatibility(d, 2, BIG_BUDGET);
    assert(!r.ok);
    assert(r.conflict == DeviceConflict::IO_PORT_CONFLICT);
    assert(r.device_a == 0);
    assert(r.device_b == 1);
    printf("PASS: test_two_psgs_io_conflict\n");
}

static void test_scc_v9990_same_subslot_conflict() {
    // SCC and V9990 both memory-mapped in subslot 2 — conflict.
    DeviceCheckEntry d[] = {
        { find_peripheral_by_name("scc"),         2 },
        { find_peripheral_by_name("v9990"),       2 },
    };
    DeviceCheckResult r = check_device_compatibility(d, 2, BIG_BUDGET);
    assert(!r.ok);
    assert(r.conflict == DeviceConflict::SUBSLOT_CONFLICT);
    printf("PASS: test_scc_v9990_same_subslot_conflict\n");
}

static void test_scc_v9990_different_subslots() {
    // Different subslots — no conflict.
    DeviceCheckEntry d[] = {
        { find_peripheral_by_name("scc"),         1 },
        { find_peripheral_by_name("v9990"),       2 },
    };
    DeviceCheckResult r = check_device_compatibility(d, 2, BIG_BUDGET);
    assert(r.ok);
    printf("PASS: test_scc_v9990_different_subslots\n");
}

static void test_psg_io_memory_no_subslot_conflict() {
    // PSG (IO) and SCC (memory-mapped) — no subslot or port conflict.
    DeviceCheckEntry d[] = {
        { find_peripheral_by_name("psg"),         0 },
        { find_peripheral_by_name("scc"),         0 },
    };
    DeviceCheckResult r = check_device_compatibility(d, 2, BIG_BUDGET);
    assert(r.ok);
    printf("PASS: test_psg_io_memory_no_subslot_conflict\n");
}

static void test_sram_exceeded() {
    // OPL4 needs 5120 bytes; budget of 100 is too small.
    DeviceCheckEntry d[] = {{ find_peripheral_by_name("opl4"),        0 }};
    DeviceCheckResult r = check_device_compatibility(d, 1, 100u);
    assert(!r.ok);
    assert(r.conflict == DeviceConflict::SRAM_EXCEEDED);
    assert(r.device_a == 0);
    printf("PASS: test_sram_exceeded\n");
}

static void test_unknown_type() {
    DeviceCheckEntry d[] = {{ nullptr,                              0 }};
    DeviceCheckResult r = check_device_compatibility(d, 1, BIG_BUDGET);
    assert(!r.ok);
    assert(r.conflict == DeviceConflict::UNKNOWN_TYPE);
    assert(r.device_a == 0);
    printf("PASS: test_unknown_type\n");
}

int main() {
    test_empty();
    test_single_psg();
    test_psg_opl4_compatible();
    test_two_psgs_io_conflict();
    test_scc_v9990_same_subslot_conflict();
    test_scc_v9990_different_subslots();
    test_psg_io_memory_no_subslot_conflict();
    test_sram_exceeded();
    test_unknown_type();
    printf("All test_device_check tests passed.\n");
    return 0;
}

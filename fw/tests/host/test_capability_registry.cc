// test_capability_registry.cc — unit tests for CapabilityRegistry.
#include "test_helpers.h"
#include "spine/capability_registry.h"
#include "boards/board_descriptor.h"
#include "drivers/driver_descriptor.h"
#include "policy/policy_types.h"
#include <cstring>

// ---------------------------------------------------------------------------
// Minimal test fixtures
// ---------------------------------------------------------------------------

static const BoardCapabilityDecl kTestBoardCaps[] = {
    { "hw.alpha",   false },
    { "hw.beta",    true  },
    { "hw.gamma",   false },
};
static const BoardDescriptor kTestBoard = {
    "test_board",
    kTestBoardCaps,
    3,
};

static const DriverDescriptor kTestDrivers[] = {
    { "drv.one"   },
    { "drv.two"   },
};
static constexpr size_t kTestDriverCount = 2;

static PolicyInfo make_policy(PolicyFlags flags) {
    PolicyInfo p = {};
    p.flags   = flags;
    p.version = 1;
    return p;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_registry_init_sets_initialized() {
    CapabilityRegistry reg;
    CHECK(!reg.initialized());
    reg.init(kTestBoard, kTestDrivers, kTestDriverCount, make_policy(0));
    CHECK(reg.initialized());
}

static void test_declared_count() {
    CapabilityRegistry reg;
    reg.init(kTestBoard, kTestDrivers, kTestDriverCount, make_policy(0));
    // 3 board caps + 2 driver caps = 5 declared
    CHECK(reg.declared_count() == 5);
}

static void test_is_declared_true_for_hw() {
    CapabilityRegistry reg;
    reg.init(kTestBoard, kTestDrivers, kTestDriverCount, make_policy(0));
    CHECK(reg.is_declared("hw.alpha"));
    CHECK(reg.is_declared("hw.beta"));
    CHECK(reg.is_declared("hw.gamma"));
}

static void test_is_declared_true_for_drivers() {
    CapabilityRegistry reg;
    reg.init(kTestBoard, kTestDrivers, kTestDriverCount, make_policy(0));
    CHECK(reg.is_declared("drv.one"));
    CHECK(reg.is_declared("drv.two"));
}

static void test_is_declared_false_for_unknown() {
    CapabilityRegistry reg;
    reg.init(kTestBoard, kTestDrivers, kTestDriverCount, make_policy(0));
    CHECK(!reg.is_declared("hw.nonexistent"));
    CHECK(!reg.is_declared(""));
    CHECK(!reg.is_declared("drv.three"));
}

static void test_stage3_all_declared_are_allowed() {
    // Stage 3: is_masked_by_policy always returns false.
    CapabilityRegistry reg;
    reg.init(kTestBoard, kTestDrivers, kTestDriverCount, make_policy(0));
    CHECK(reg.allowed_count() == reg.declared_count());
    CHECK(reg.is_allowed("hw.alpha"));
    CHECK(reg.is_allowed("hw.beta"));
    CHECK(reg.is_allowed("hw.gamma"));
    CHECK(reg.is_allowed("drv.one"));
    CHECK(reg.is_allowed("drv.two"));
}

static void test_is_allowed_false_for_undeclared() {
    CapabilityRegistry reg;
    reg.init(kTestBoard, kTestDrivers, kTestDriverCount, make_policy(0));
    CHECK(!reg.is_allowed("hw.nonexistent"));
}

static void test_list_declared_fills_array() {
    CapabilityRegistry reg;
    reg.init(kTestBoard, kTestDrivers, kTestDriverCount, make_policy(0));

    const char* names[8] = {};
    size_t n = reg.list_declared(names, 8);
    CHECK(n == 5);
    // All returned names must be non-null and non-empty.
    for (size_t i = 0; i < n; i++) {
        CHECK(names[i] != nullptr);
        CHECK(strlen(names[i]) > 0);
    }
}

static void test_list_declared_respects_max() {
    CapabilityRegistry reg;
    reg.init(kTestBoard, kTestDrivers, kTestDriverCount, make_policy(0));

    const char* names[2] = {};
    size_t n = reg.list_declared(names, 2);
    CHECK(n == 2);
}

static void test_list_allowed_matches_declared_in_stage3() {
    CapabilityRegistry reg;
    reg.init(kTestBoard, kTestDrivers, kTestDriverCount, make_policy(0));

    const char* decl[8] = {};
    const char* allowed[8] = {};
    size_t nd = reg.list_declared(decl, 8);
    size_t na = reg.list_allowed(allowed, 8);
    CHECK(nd == na);
}

static void test_empty_board_and_no_drivers() {
    static const BoardDescriptor kEmpty = { "empty", nullptr, 0 };
    CapabilityRegistry reg;
    reg.init(kEmpty, nullptr, 0, make_policy(0));
    CHECK(reg.declared_count() == 0);
    CHECK(reg.allowed_count() == 0);
    CHECK(!reg.is_declared("anything"));
    CHECK(!reg.is_allowed("anything"));
}

static void test_real_board_descriptor() {
    // Verify the actual board descriptor compiles and returns something sensible.
    const BoardDescriptor& board = BoardDescriptor::for_current_board();
    CHECK(board.board_id != nullptr);
    CHECK(board.capability_count > 0);
    CHECK(board.capabilities != nullptr);

    CapabilityRegistry reg;
    PolicyInfo policy = make_policy(0);
    reg.init(board, nullptr, 0, policy);
    CHECK(reg.declared_count() == board.capability_count);
    CHECK(reg.is_declared("hw.msx_bus"));
}

int main() {
    test_registry_init_sets_initialized();
    test_declared_count();
    test_is_declared_true_for_hw();
    test_is_declared_true_for_drivers();
    test_is_declared_false_for_unknown();
    test_stage3_all_declared_are_allowed();
    test_is_allowed_false_for_undeclared();
    test_list_declared_fills_array();
    test_list_declared_respects_max();
    test_list_allowed_matches_declared_in_stage3();
    test_empty_board_and_no_drivers();
    test_real_board_descriptor();
    return test_summary();
}

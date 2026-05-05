// test_menu_stub.cc — Stage 11 host tests for menu stub embedding.
//
// Verifies that:
//   1. kMenuStubBin_SIZE is within the 2 KB stub slot.
//   2. MenuMailbox::init() copies the stub to page + MENU_STUB_OFS.
//   3. MenuStubHeader fields are set correctly after init().
//   4. Data exchange buffer is not overwritten by stub placement.

#include "msx/menu/menu_host_abi.h"
#include "msx/menu/menu_host_abi.h"

#include <cassert>
#include <cstdio>
#include <cstring>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, #expr); \
            ++g_fail; \
        } else { \
            ++g_pass; \
        } \
    } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))

// ---------------------------------------------------------------------------
// Test 1: stub binary size constraint
// ---------------------------------------------------------------------------

static void test_stub_size_within_limit() {
    // Stub must fit in the 2 KB slot at MENU_STUB_OFS.
    CHECK(kMenuStubBin_SIZE <= 2048u);
}

// ---------------------------------------------------------------------------
// Test 2: stub embedded at MENU_STUB_OFS after init()
// ---------------------------------------------------------------------------

static void test_stub_embedded_at_correct_offset() {
    static uint8_t page[MENU_PAGE_SIZE];
    memset(page, 0xAA, sizeof(page));  // sentinel pattern

    MenuMailbox mbx;
    mbx.init(page, 0u /* ignored */);

    if (kMenuStubBin_SIZE == 0u) {
        // Placeholder build: stub slot should be zeroed (from memset inside init).
        CHECK(page[MENU_STUB_OFS] == 0x00u);
        return;
    }

    // First byte of stub binary must match what was placed in page.
    CHECK_EQ(page[MENU_STUB_OFS], kMenuStubBin[0]);

    // Last byte of stub binary.
    CHECK_EQ(page[MENU_STUB_OFS + kMenuStubBin_SIZE - 1u],
             kMenuStubBin[kMenuStubBin_SIZE - 1u]);

    // Bytes beyond the stub (within the 2 KB slot) must be zero
    // (init zeroes the whole page, stub is then copied in).
    if (kMenuStubBin_SIZE < 2048u) {
        CHECK_EQ(page[MENU_STUB_OFS + kMenuStubBin_SIZE], 0x00u);
    }
}

// ---------------------------------------------------------------------------
// Test 3: MenuStubHeader fields
// ---------------------------------------------------------------------------

static void test_header_fields() {
    static uint8_t page[MENU_PAGE_SIZE];
    memset(page, 0, sizeof(page));

    MenuMailbox mbx;
    mbx.init(page, 0x1234u /* ignored — must use MENU_STUB_OFS */);

    const MenuStubHeader* hdr = reinterpret_cast<const MenuStubHeader*>(page);

    CHECK(hdr->sig[0] == 'J');
    CHECK(hdr->sig[1] == 'L');
    CHECK(hdr->sig[2] == 'M');
    CHECK(hdr->sig[3] == 'N');
    CHECK_EQ(hdr->abi_major,   MENU_ABI_MAJOR);
    CHECK_EQ(hdr->abi_minor,   MENU_ABI_MINOR);
    CHECK_EQ(hdr->header_len,  static_cast<uint16_t>(sizeof(MenuStubHeader)));
    CHECK_EQ(hdr->mailbox_ofs, MENU_MAILBOX_OFS);
    CHECK_EQ(hdr->data_ofs,    MENU_DATA_OFS);
    CHECK_EQ(hdr->data_len,    MENU_USABLE_DATA_LEN);
    // stub_entry must always be MENU_STUB_OFS regardless of the parameter passed.
    CHECK_EQ(hdr->stub_entry,  MENU_STUB_OFS);
    // host_caps and vdp_caps start at zero; filled by Z80 stub at runtime.
    CHECK_EQ(hdr->host_caps, 0u);
    CHECK_EQ(hdr->vdp_caps,  0u);
}

// ---------------------------------------------------------------------------
// Test 4: data buffer area not clobbered by stub placement
// ---------------------------------------------------------------------------

static void test_data_buffer_zeroed() {
    static uint8_t page[MENU_PAGE_SIZE];
    memset(page, 0xBB, sizeof(page));

    MenuMailbox mbx;
    mbx.init(page, 0u);

    // init() does memset(page, 0, MENU_PAGE_SIZE) first, then copies stub.
    // The data buffer area (MENU_DATA_OFS .. MENU_STUB_OFS - 1) must be zero.
    for (uint16_t i = MENU_DATA_OFS; i < MENU_STUB_OFS; ++i) {
        if (page[i] != 0x00u) {
            fprintf(stderr,
                    "FAIL: page[0x%04X] = 0x%02X, expected 0 (data buffer not clean)\n",
                    i, page[i]);
            ++g_fail;
            return;
        }
    }
    ++g_pass;
}

// ---------------------------------------------------------------------------
// Test 5: MENU_USABLE_DATA_LEN + stub slot == full data range
// ---------------------------------------------------------------------------

static void test_layout_arithmetic() {
    // MENU_DATA_OFS + MENU_USABLE_DATA_LEN == MENU_STUB_OFS
    CHECK_EQ(static_cast<uint32_t>(MENU_DATA_OFS) + MENU_USABLE_DATA_LEN,
             static_cast<uint32_t>(MENU_STUB_OFS));
    // MENU_STUB_OFS + 2048 == MENU_PAGE_SIZE
    CHECK_EQ(static_cast<uint32_t>(MENU_STUB_OFS) + 2048u,
             static_cast<uint32_t>(MENU_PAGE_SIZE));
}

// ---------------------------------------------------------------------------
// Test 6: mailbox starts idle
// ---------------------------------------------------------------------------

static void test_mailbox_idle_after_init() {
    static uint8_t page[MENU_PAGE_SIZE];
    MenuMailbox mbx;
    mbx.init(page, 0u);

    CHECK(mbx.initialized());
    CHECK(!mbx.pending());

    const MenuMailboxRegs* regs = mbx.raw_regs();
    CHECK_EQ(regs->cmd_seq,  0u);
    CHECK_EQ(regs->resp_seq, 0u);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    test_stub_size_within_limit();
    test_stub_embedded_at_correct_offset();
    test_header_fields();
    test_data_buffer_zeroed();
    test_layout_arithmetic();
    test_mailbox_idle_after_init();

    printf("RESULTS: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}

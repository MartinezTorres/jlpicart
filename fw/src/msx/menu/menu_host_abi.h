#pragma once
// menu_host_abi.h — Menu Host ABI: shared page layout, mailbox protocol,
// command set, and RP2350-side controller class.
//
// The Menu Page is a 16KB SRAM buffer that the bus layer maps at Z80 page 1
// (0x4000–0x7FFF, subslot 1) when the Menu is active.  The RP2350 writes
// commands into the MenuMailboxRegs; the Z80 stub executes them and writes
// results back.  Both sides share the data buffer at data_ofs.
//
// Spec reference: spec.md §8 (Menu Host ABI).
// Bootstrapping:  bootstrapping.md §5.

#include <cstdint>
#include <cstring>

// ---------------------------------------------------------------------------
// Page layout constants (spec.md §8 "Fixed layout within the 16KB Menu Page")
// ---------------------------------------------------------------------------

static constexpr uint16_t MENU_PAGE_SIZE       = 0x4000u; // 16 KB
static constexpr uint16_t MENU_HEADER_OFS      = 0x0000u; // MenuStubHeader
static constexpr uint16_t MENU_MAILBOX_OFS     = 0x0040u; // MenuMailboxRegs
static constexpr uint16_t MENU_DATA_OFS        = 0x0100u; // shared data buffer (spec: MUST be 0x0100)
static constexpr uint16_t MENU_STUB_OFS        = 0x3800u; // stub code at top of page (2 KB slot)
static constexpr uint16_t MENU_DATA_LEN        = MENU_PAGE_SIZE - MENU_DATA_OFS; // 0x3F00 (full range)
static constexpr uint16_t MENU_USABLE_DATA_LEN = MENU_STUB_OFS - MENU_DATA_OFS;  // 0x3700 (excl. stub)

static constexpr uint8_t  MENU_ABI_MAJOR     = 1u;
static constexpr uint8_t  MENU_ABI_MINOR     = 0u;

// ---------------------------------------------------------------------------
// Command IDs (spec.md §8 "Command IDs and semantics")
// ---------------------------------------------------------------------------

static constexpr uint16_t MENU_CMD_NOP           = 0x0000u;
static constexpr uint16_t MENU_CMD_GET_HOST_INFO = 0x0001u;
static constexpr uint16_t MENU_CMD_SET_MODE      = 0x0002u;
static constexpr uint16_t MENU_CMD_CLEAR         = 0x0003u;
static constexpr uint16_t MENU_CMD_PUT_TEXT      = 0x0004u;
static constexpr uint16_t MENU_CMD_READ_INPUT    = 0x0005u;
static constexpr uint16_t MENU_CMD_VDP_WRITE_REG = 0x0006u;
static constexpr uint16_t MENU_CMD_VRAM_WRITE    = 0x0007u;
static constexpr uint16_t MENU_CMD_VRAM_FILL     = 0x0008u;
static constexpr uint16_t MENU_CMD_BEEP          = 0x0009u;
static constexpr uint16_t MENU_CMD_IDLE          = 0x000Au;

// SET_MODE mode ids (arg0 low byte)
static constexpr uint8_t  MENU_MODE_TEXT_40 = 0u; // SCREEN 0, 40 cols — MUST be supported
static constexpr uint8_t  MENU_MODE_TEXT_80 = 1u; // SCREEN 0, 80 cols — MAY be supported
static constexpr uint8_t  MENU_MODE_BITMAP  = 2u; // best-effort bitmap — MAY be supported

// CLEAR kind (arg0 low byte)
static constexpr uint8_t  MENU_CLEAR_ALL    = 0u; // clear text/bitmap to background
static constexpr uint8_t  MENU_CLEAR_TEXT   = 1u; // clear text area only (text modes)
static constexpr uint8_t  MENU_CLEAR_BITMAP = 2u; // clear bitmap plane only

// ---------------------------------------------------------------------------
// Status codes (spec.md §8 "Error codes (Menu mailbox status)")
// ---------------------------------------------------------------------------

static constexpr uint16_t MENU_OK           = 0x0000u;
static constexpr uint16_t MENU_E_UNSUPPORTED= 0x0001u; // unknown command or feature
static constexpr uint16_t MENU_E_BAD_ARG    = 0x0002u; // invalid argument or length
static constexpr uint16_t MENU_E_BAD_STATE  = 0x0003u; // command invalid in current mode
static constexpr uint16_t MENU_E_OVERFLOW   = 0x0004u; // in_len/out_len exceeds data_len
static constexpr uint16_t MENU_E_HW        = 0x0005u; // VDP/BIOS call failed (best-effort)

// ---------------------------------------------------------------------------
// host_caps bits (spec.md §8 MenuStubHeader.host_caps)
// Filled by the Z80 stub at init; read by the RP2350 after stub runs.
// ---------------------------------------------------------------------------

static constexpr uint32_t MENU_HOST_CAP_MSX1          = (1u << 0);
static constexpr uint32_t MENU_HOST_CAP_MSX2          = (1u << 1);
static constexpr uint32_t MENU_HOST_CAP_MSX2PLUS      = (1u << 2);
static constexpr uint32_t MENU_HOST_CAP_TURBOR        = (1u << 3);
static constexpr uint32_t MENU_HOST_CAP_TEXT_80       = (1u << 4);
static constexpr uint32_t MENU_HOST_CAP_BIOS_KBD      = (1u << 5);

// vdp_caps bits
static constexpr uint32_t MENU_VDP_CAP_TMS9918        = (1u << 0); // TMS9918/V9938 VRAM port writes
static constexpr uint32_t MENU_VDP_CAP_PALETTE        = (1u << 1); // palette programmable (MSX2+)
static constexpr uint32_t MENU_VDP_CAP_BITMAP         = (1u << 2); // bitmap mode supported

// ---------------------------------------------------------------------------
// Packed structs — layout identical on RP2350 and Z80
// ---------------------------------------------------------------------------

#pragma pack(push, 1)

// spec.md §8 "Header layout (MenuStubHeader)" — 64 bytes at MENU_HEADER_OFS.
//
// NOTE: The spec lists reserved[20] which only accounts for 48 bytes.  This
// is a spec gap; reserved[36] is used here to reach exactly 64 bytes and
// match the stated layout range 0x0000..0x003F.
struct MenuStubHeader {
    char     sig[4];          // "JLMN"
    uint8_t  abi_major;       // MENU_ABI_MAJOR
    uint8_t  abi_minor;       // MENU_ABI_MINOR
    uint16_t header_len;      // MUST be 64
    uint16_t mailbox_ofs;     // MUST be MENU_MAILBOX_OFS (0x0040)
    uint16_t data_ofs;        // MUST be MENU_DATA_OFS    (0x0100)
    uint16_t data_len;        // MENU_DATA_LEN (0x3F00)
    uint16_t stub_entry;      // offset of stub entrypoint within this page
    uint32_t host_caps;       // MENU_HOST_CAP_* — filled by Z80 stub at init
    uint32_t vdp_caps;        // MENU_VDP_CAP_*  — filled by Z80 stub at init
    uint16_t build_id;        // implementation-defined
    uint16_t reserved0;
    uint8_t  reserved[36];    // pads struct to exactly 64 bytes
};
static_assert(sizeof(MenuStubHeader) == 64, "MenuStubHeader must be 64 bytes");

// spec.md §8 "Mailbox layout (MenuMailbox)" — 64 bytes at MENU_MAILBOX_OFS.
// Named MenuMailboxRegs to avoid collision with the C++ controller class.
struct MenuMailboxRegs {
    volatile uint16_t cmd_seq;   // RP2350 writes last when posting a command
    volatile uint16_t resp_seq;  // stub writes last when command is done
    volatile uint16_t cmd_id;
    volatile uint16_t status;    // MENU_OK or error code
    volatile uint32_t arg0;      // packed x(0:7)+y(8:15) by convention
    volatile uint32_t arg1;      // packed w(0:7)+h(8:15) by convention
    volatile uint32_t arg2;
    volatile uint32_t arg3;
    volatile uint16_t in_len;    // bytes provided in shared data buffer
    volatile uint16_t out_len;   // bytes written back by stub
    volatile uint8_t  reserved[36]; // pads to 64 bytes
};
static_assert(sizeof(MenuMailboxRegs) == 64, "MenuMailboxRegs must be 64 bytes");

// spec.md §8 "HostInfo structure" — returned by GET_HOST_INFO.
struct HostInfo {
    uint8_t  msx_gen;      // 1=MSX1, 2=MSX2, 3=MSX2+, 4=turboR (best-effort)
    uint8_t  vram_kb;      // 16, 64, 128... (best-effort)
    uint8_t  text_cols;    // 40 or 80 (current or best supported)
    uint8_t  reserved0;
    uint32_t host_caps;    // MENU_HOST_CAP_* — mirrors MenuStubHeader.host_caps
    uint32_t vdp_caps;     // MENU_VDP_CAP_*  — mirrors MenuStubHeader.vdp_caps
};
static_assert(sizeof(HostInfo) == 12, "HostInfo must be 12 bytes");

// spec.md §8 "InputSnapshot structure" — returned by READ_INPUT.
struct InputSnapshot {
    uint8_t kbd_rows[11]; // rows 0–10, 8 bits each, 0=pressed, 1=released
    uint8_t joy1;         // b0=Up b1=Down b2=Left b3=Right b4=TrigA b5=TrigB, 0=pressed
    uint8_t joy2;         // same mapping
    uint8_t reserved[3];
};
static_assert(sizeof(InputSnapshot) == 16, "InputSnapshot must be 16 bytes");

#pragma pack(pop)

// ---------------------------------------------------------------------------
// MenuMailbox — RP2350-side controller for the Menu Host ABI
// ---------------------------------------------------------------------------
//
// Wraps a 16KB page buffer and provides the RP2350 half of the mailbox
// protocol.  The Z80 stub is the executor; the RP2350 is the command issuer.
//
// Protocol per spec.md §8:
//   - RP2350 writes command fields first, then cmd_seq last ("post").
//   - Stub detects cmd_seq != resp_seq, executes, writes resp_seq last.
//   - RP2350 calls tick() to collect the response.

class MenuMailbox {
public:
    // Initialise the controller with a pointer to a MENU_PAGE_SIZE byte buffer.
    // Writes the MenuStubHeader and zeros the mailbox registers.
    // stub_entry is the page-relative offset of the Z80 stub entrypoint.
    void init(uint8_t* page, uint16_t stub_entry);

    bool initialized() const { return initialized_; }

    // Post one command.  Returns false if a previous command is still pending.
    // in_data/in_len: optional payload copied into the shared data buffer.
    bool send_command(uint16_t cmd_id,
                      uint32_t arg0 = 0u, uint32_t arg1 = 0u,
                      uint32_t arg2 = 0u, uint32_t arg3 = 0u,
                      const uint8_t* in_data = nullptr, uint16_t in_len = 0u);

    // Check whether the stub has responded to the last posted command.
    // If so, saves status and out_len and returns true.
    // Returns false if no command is pending or the stub has not yet responded.
    bool tick();

    bool     pending()      const { return pending_; }
    uint16_t last_status()  const { return last_status_; }
    uint16_t last_out_len() const { return last_out_len_; }

    // Non-zero once the Z80 stub has completed its own initialisation.
    // The stub writes host_caps to the header before writing resp_seq=cmd_seq
    // at startup (spec §8), so any non-zero value means the stub is ready.
    uint32_t stub_host_caps() const { return initialized_ ? hdr_->host_caps : 0u; }

    // Shared data buffer — valid read after tick() returns true.
    const uint8_t* data_buf()      const { return data_; }
    uint16_t       data_capacity() const { return MENU_DATA_LEN; }

    // Raw register access used by host tests to simulate Z80 responses.
    MenuMailboxRegs* raw_regs() { return mbx_; }

private:
    MenuStubHeader* hdr_          = nullptr;
    MenuMailboxRegs* mbx_         = nullptr;
    uint8_t*         data_        = nullptr;
    bool             initialized_ = false;
    bool             pending_     = false;
    uint16_t         last_status_ = 0u;
    uint16_t         last_out_len_= 0u;
};

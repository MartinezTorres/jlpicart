#pragma once
// api_types.h — packed structs and constants for the JLPiCart API window.
//
// All structs are #pragma pack(1), little-endian, shared between RP2350 firmware
// and Z80 client code.  No SDK types; this file is included in host tests too.
//
// Spec references: spec.md §5 (JLPiCart API), §5.1 (Fixed MSX-visible allocations).

#include <cstdint>
#include <cstddef>

// ---------------------------------------------------------------------------
// Window layout constants (spec.md §5.1 "Fixed layout within the 16KB API Window")
// ---------------------------------------------------------------------------

static constexpr uint16_t API_WINDOW_SIZE      = 0x4000u; // 16 KB total
static constexpr uint16_t API_HEADER_OFS       = 0x0000u; // ApiWindowHeader
static constexpr uint16_t API_REGS_OFS         = 0x0040u; // ApiRegs (32 bytes)
static constexpr uint16_t API_RING_DATA_SIZE   = 512u;    // ring data bytes (power of two)
static constexpr uint16_t API_RING_HDR_SIZE    = 8u;      // sizeof(RingHeader)
static constexpr uint16_t API_RING_TOTAL       = API_RING_HDR_SIZE + API_RING_DATA_SIZE; // 520

static constexpr uint16_t API_REQ_RING_OFS     = 0x0060u;
static constexpr uint16_t API_REQ_RING_LEN     = API_RING_TOTAL;
static constexpr uint16_t API_RSP_RING_OFS     = API_REQ_RING_OFS + API_REQ_RING_LEN;   // 0x0268
static constexpr uint16_t API_RSP_RING_LEN     = API_RING_TOTAL;
static constexpr uint16_t API_H2C_SCRATCH_OFS  = API_RSP_RING_OFS + API_RSP_RING_LEN;   // 0x0470
static constexpr uint16_t API_H2C_SCRATCH_LEN  = 1024u;
static constexpr uint16_t API_C2H_SCRATCH_OFS  = API_H2C_SCRATCH_OFS + API_H2C_SCRATCH_LEN; // 0x0870
static constexpr uint16_t API_C2H_SCRATCH_LEN  = 1024u;

// Maximum frame size in bytes, including the 2-byte length prefix.
// Must fit contiguously in the ring without wrap (API_RING_DATA_SIZE / 2 is safe).
static constexpr uint16_t API_MAX_FRAME        = 256u;
static constexpr uint16_t API_MAX_MSG          = API_MAX_FRAME - 2u; // max msg_bytes

// ---------------------------------------------------------------------------
// API version and flags
// ---------------------------------------------------------------------------

static constexpr uint8_t API_MAJOR      = 1u;
static constexpr uint8_t API_MINOR      = 0u;
static constexpr uint8_t API_LAYOUT_VER = 1u;

// ApiWindowHeader.flags
static constexpr uint8_t API_HDR_FLAG_RINGS = (1u << 0); // rings enabled (MUST be 1)
static constexpr uint8_t API_HDR_FLAG_REGS  = (1u << 1); // ApiRegs doorbells enabled
static constexpr uint8_t API_HDR_FLAGS_V1   = API_HDR_FLAG_RINGS | API_HDR_FLAG_REGS;

// feature_bits (ApiWindowHeader and ApiInfo): which services are implemented.
// spec.md "Feature bits (in feature_bits)".
static constexpr uint32_t API_FEATURE_SYSTEM    = (1u << 0); // System service 0x00
static constexpr uint32_t API_FEATURE_STORAGE   = (1u << 1); // Storage service 0x01 (Stage 6)
static constexpr uint32_t API_FEATURE_NETWORK   = (1u << 2); // Network service 0x02 (future)
static constexpr uint32_t API_FEATURE_IDENTITY  = (1u << 3); // Identity service 0x03 (future)
static constexpr uint32_t API_FEATURE_USERSTATS = (1u << 4); // UserStats 0x04 (future)

// Stage 4: only System service implemented.
static constexpr uint32_t API_FEATURES_STAGE4  = API_FEATURE_SYSTEM;
// Stage 15: adds Identity service.
static constexpr uint32_t API_FEATURES_STAGE15 = API_FEATURE_SYSTEM | API_FEATURE_IDENTITY;
// Stage 19: adds Storage service.
static constexpr uint32_t API_FEATURES_STAGE19 =
    API_FEATURE_SYSTEM | API_FEATURE_IDENTITY | API_FEATURE_STORAGE;
// Stage 21: adds UserStats service.
static constexpr uint32_t API_FEATURES_STAGE21 =
    API_FEATURE_SYSTEM | API_FEATURE_IDENTITY | API_FEATURE_STORAGE | API_FEATURE_USERSTATS;

// ---------------------------------------------------------------------------
// Packed structs — layout identical on RP2350 and Z80
// ---------------------------------------------------------------------------

#pragma pack(push, 1)

// spec.md §5.1 "ApiWindowHeader (64 bytes)"
struct ApiWindowHeader {
    char     sig[4];           // "JLP1"
    uint8_t  api_major;        // API_MAJOR
    uint8_t  api_minor;        // API_MINOR
    uint8_t  layout_ver;       // API_LAYOUT_VER
    uint8_t  flags;            // API_HDR_FLAG_*
    uint16_t win_size;         // API_WINDOW_SIZE (0x4000)
    uint16_t regs_ofs;         // API_REGS_OFS   (0x0040)
    uint16_t req_ring_ofs;
    uint16_t req_ring_len;
    uint16_t rsp_ring_ofs;
    uint16_t rsp_ring_len;
    uint16_t h2c_scratch_ofs;
    uint16_t h2c_scratch_len;
    uint16_t c2h_scratch_ofs;
    uint16_t c2h_scratch_len;
    uint32_t feature_bits;     // API_FEATURE_* bitmap
    uint16_t max_frame;        // API_MAX_FRAME
    uint16_t reserved0;        // MUST be 0
    uint32_t reserved1;        // MUST be 0
    uint8_t  reserved2[24];    // pads struct to exactly 64 bytes
};
static_assert(sizeof(ApiWindowHeader) == 64, "ApiWindowHeader must be 64 bytes");

// spec.md §5.1 "ApiRegs (32 bytes)"
struct ApiRegs {
    volatile uint8_t  host_kick;    // host increments to notify "requests posted"
    volatile uint8_t  cart_event;   // cart increments to notify "responses posted"
    volatile uint8_t  host_flags;   // reserved (MUST write 0)
    volatile uint8_t  cart_flags;   // bit0=error_latched, bit1=busy (best-effort)
    volatile uint16_t last_err;     // last error code (best-effort)
    volatile uint16_t last_seq;     // seq associated with last_err (best-effort)
    volatile uint8_t  reserved[24]; // MUST be 0 in v1
};
static_assert(sizeof(ApiRegs) == 32, "ApiRegs must be 32 bytes");

// spec.md §5.1 "RingHeader"
struct RingHeader {
    volatile uint16_t head;  // producer writes (byte offset into ring_data)
    volatile uint16_t tail;  // consumer writes (byte offset into ring_data)
    uint16_t          size;  // ring_data size in bytes (power of two; set at init)
    uint16_t          flags; // reserved, MUST be 0 in v1
    // uint8_t ring_data[size] follows in memory
};
static_assert(sizeof(RingHeader) == 8, "RingHeader must be 8 bytes");

// spec.md §5.1 "Message header (MsgHeader, 16 bytes)"
struct MsgHeader {
    uint16_t seq;          // host-chosen request id; echoed in response
    uint8_t  service;      // service id (SVC_*)
    uint8_t  method;       // method id within service
    uint16_t flags;        // request/response flags (0 in requests)
    uint16_t status;       // response status code; MUST be 0 in requests
    uint16_t payload_len;  // bytes after this header
    uint16_t scratch_ofs;  // offset within direction scratch; 0xFFFF = none
    uint16_t scratch_len;  // valid bytes in scratch
    uint16_t reserved;     // MUST be 0
};
static_assert(sizeof(MsgHeader) == 16, "MsgHeader must be 16 bytes");

// ---------------------------------------------------------------------------
// Status codes (spec.md "Status codes")
// ---------------------------------------------------------------------------

static constexpr uint16_t API_OK            = 0x0000u;
static constexpr uint16_t API_E_BAD_REQ     = 0x1001u; // malformed frame or header
static constexpr uint16_t API_E_BAD_ARG     = 0x1002u; // invalid argument values
static constexpr uint16_t API_E_UNSUPPORTED = 0x1003u; // unknown service or method
static constexpr uint16_t API_E_DENIED      = 0x1004u; // policy denied or not authenticated
static constexpr uint16_t API_E_NOT_FOUND   = 0x1005u; // unknown id or handle
static constexpr uint16_t API_E_BUSY        = 0x1006u; // resource busy
static constexpr uint16_t API_E_TIMEOUT     = 0x1007u; // operation exceeded limit
static constexpr uint16_t API_E_RING_FULL   = 0x1008u; // cannot enqueue frame
static constexpr uint16_t API_E_TOO_BIG     = 0x1009u; // payload or scratch too large
static constexpr uint16_t API_E_INTERNAL    = 0x2001u; // cart-side runtime error
static constexpr uint16_t API_E_POLICY      = 0x1010u; // operation blocked by policy flag

// ---------------------------------------------------------------------------
// Service IDs (spec.md "Services")
// ---------------------------------------------------------------------------

static constexpr uint8_t SVC_SYSTEM   = 0x00u;
static constexpr uint8_t SVC_STORAGE  = 0x01u;
static constexpr uint8_t SVC_NETWORK  = 0x02u;
static constexpr uint8_t SVC_IDENTITY = 0x03u;
static constexpr uint8_t SVC_USERSTATS = 0x04u;

// ---------------------------------------------------------------------------
// System service (0x00) method IDs (spec.md "System service")
// ---------------------------------------------------------------------------

static constexpr uint8_t SYS_GET_API_INFO      = 0x00u;
static constexpr uint8_t SYS_GET_DEVICE_ID     = 0x01u;
static constexpr uint8_t SYS_GET_CAPS          = 0x02u;
static constexpr uint8_t SYS_GET_RANDOM        = 0x03u;
static constexpr uint8_t SYS_RESET_TO_MENU     = 0x04u;
static constexpr uint8_t SYS_GET_SECURITY_INFO = 0x05u;
static constexpr uint8_t SYS_GET_POLICY_FLAGS  = 0x06u;

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Storage service (0x01) method IDs  (spec.md "Storage service")
// ---------------------------------------------------------------------------

static constexpr uint8_t STG_LIST_BLOBS        = 0x00u;
static constexpr uint8_t STG_READ_BLOB         = 0x01u;
static constexpr uint8_t STG_WRITE_BLOB_BEGIN  = 0x02u;
static constexpr uint8_t STG_WRITE_BLOB_CHUNK  = 0x03u;
static constexpr uint8_t STG_WRITE_BLOB_COMMIT = 0x04u;
static constexpr uint8_t STG_DELETE_BLOB       = 0x05u;

// ---------------------------------------------------------------------------
// Identity service (0x03) method IDs  (spec.md "Identity service")
// ---------------------------------------------------------------------------

static constexpr uint8_t IDN_LIST_PROFILES       = 0x00u;
static constexpr uint8_t IDN_SET_ACTIVE_PROFILE  = 0x01u;
static constexpr uint8_t IDN_GET_ACTIVE_PROFILE  = 0x02u;
static constexpr uint8_t IDN_GUEST_BEGIN         = 0x03u;
static constexpr uint8_t IDN_GUEST_END           = 0x04u;

// Max name bytes returned in a LIST_PROFILES entry (must fit within API_MAX_MSG
// when encoding PROF_MAX_PROFILES=8 entries with a 16-byte MsgHeader).
// Payload budget: API_MAX_MSG(254) - sizeof(MsgHeader)(16) = 238 bytes.
// With 8 profiles: 2 (count) + 8×(2+1+name_len) ≤ 238 → name_len ≤ 26.
// Use 16 bytes for a comfortable fit.
static constexpr uint8_t PROF_API_NAME_MAX = 16u;

// ---------------------------------------------------------------------------
// UserStats service (0x04) method IDs  (spec.md "UserStats service")
// ---------------------------------------------------------------------------

static constexpr uint8_t UST_STAT_GET          = 0x00u;
static constexpr uint8_t UST_STAT_SET          = 0x01u;
static constexpr uint8_t UST_ACH_UNLOCK        = 0x02u;
static constexpr uint8_t UST_LEADER_RUN_BEGIN  = 0x03u;
static constexpr uint8_t UST_LEADER_SUBMIT     = 0x04u;

// ---------------------------------------------------------------------------
// posture_props bitfield  (spec.md "Posture properties (OTP-derived)")
//
// Bit assignments are authoritative here; the spec says "see spec table" but
// does not yet define one.  These bits are normative from Stage 4 onward.
// ---------------------------------------------------------------------------

static constexpr uint32_t API_POSTURE_SECURE_BOOT_ENABLED    = (1u << 0);
static constexpr uint32_t API_POSTURE_OTP_SECRET_PRESENT     = (1u << 1);
static constexpr uint32_t API_POSTURE_DEBUG_DISABLED         = (1u << 2);
static constexpr uint32_t API_POSTURE_USB_BOOT_DISABLED      = (1u << 3);
static constexpr uint32_t API_POSTURE_UART_BOOT_DISABLED     = (1u << 4);
static constexpr uint32_t API_POSTURE_ANTI_ROLLBACK_ENABLED  = (1u << 5);
static constexpr uint32_t API_POSTURE_ENCRYPTED_BOOT_ENABLED = (1u << 6);
// bits 7..31: reserved, MUST be 0 in v1.

// ---------------------------------------------------------------------------
// Response payload structs (all packed, little-endian)
// ---------------------------------------------------------------------------

// System.GET_API_INFO (0x00) response  (spec.md)
struct ApiInfo {
    uint8_t  api_major;
    uint8_t  api_minor;
    uint8_t  layout_ver;
    uint8_t  flags;            // ApiWindowHeader.flags value
    uint32_t feature_bits;     // API_FEATURE_* bitmap
    uint16_t max_frame;
    uint16_t reserved0;
    uint32_t posture_props;        // API_POSTURE_* bitfield
    uint8_t  boot_key_valid_mask;  // bits 0..3 map to OTP boot key slots 0..3
    uint8_t  reserved1[3];
};
static_assert(sizeof(ApiInfo) == 20, "ApiInfo must be 20 bytes");

// ---------------------------------------------------------------------------
// Capability numeric IDs (spec.md §5.1, doc/api/capabilities.md)
//
// These assignments are stable: once an ID is assigned to a capability string
// it MUST NOT be reassigned or reused.  Domain groupings are 0x0100 wide.
// ---------------------------------------------------------------------------

// Domain 0x0000: Core platform services
static constexpr uint16_t CAP_BUS_MSX             = 0x0001u; // hw: MSX bus interface

// Domain 0x0100: Storage
static constexpr uint16_t CAP_STORAGE_EXT_FLASH   = 0x0101u; // hw: external SPI flash
static constexpr uint16_t CAP_STORAGE_MASS        = 0x0102u; // sw: Nextor block device
static constexpr uint16_t CAP_STORAGE_FLOPPY      = 0x0103u; // sw: floppy controller

// Domain 0x0200: Network
static constexpr uint16_t CAP_NET_WIFI            = 0x0201u; // hw: ESP32 WiFi via AT
static constexpr uint16_t CAP_NET_ETH             = 0x0202u; // hw: Ethernet (custom boards)
static constexpr uint16_t CAP_NET_ESP32           = 0x0203u; // sw: ESP32 AT transport driver

// Domain 0x0300: I/O
static constexpr uint16_t CAP_IO_USB_HOST         = 0x0301u; // hw: USB host port
static constexpr uint16_t CAP_IO_ADC              = 0x0302u; // hw: ADC channels
static constexpr uint16_t CAP_IO_RS232            = 0x0303u; // hw: RS232/UART (custom boards)

// Domain 0x0400: UI
static constexpr uint16_t CAP_UI_OLED             = 0x0401u; // hw: SSD1306 128x32 OLED
static constexpr uint16_t CAP_UI_EINK             = 0x0402u; // hw: e-ink display (custom boards)

// Domain 0x0500: Video
static constexpr uint16_t CAP_VIDEO_CRT           = 0x0501u; // hw: CRT/VGA analog output
static constexpr uint16_t CAP_VIDEO_V9990         = 0x0502u; // sw: V9990/G9000 VDP emulation

// Domain 0x0600: Audio
static constexpr uint16_t CAP_AUDIO_OUT           = 0x0601u; // hw: stereo DAC output
static constexpr uint16_t CAP_AUDIO_OPL4          = 0x0602u; // sw: OPL4 MoonSound emulation

// Domain 0x1000: Software capabilities
static constexpr uint16_t CAP_API_CORE            = 0x1001u; // sw: core API service
static constexpr uint16_t CAP_SW_MAPPER           = 0x1002u; // sw: MSX ROM mapper emulation
static constexpr uint16_t CAP_SW_PSG              = 0x1010u; // sw: AY-3-8910 PSG emulation
static constexpr uint16_t CAP_SW_SCC              = 0x1011u; // sw: Konami SCC/SCC+ emulation
static constexpr uint16_t CAP_SW_MENU             = 0x1020u; // sw: menu host ABI + Z80 stub

// cap_flags bits in CapEntry (doc/api/capabilities.md)
static constexpr uint16_t CAP_FLAG_ACTIVATED      = (1u << 0); // active for current session
static constexpr uint16_t CAP_FLAG_HARDWARE       = (1u << 1); // hw capability (not emulated)
static constexpr uint16_t CAP_FLAG_PROBE_OK       = (1u << 2); // hw probe succeeded

// ---------------------------------------------------------------------------
// Capability string → numeric ID mapping.
// Used by handle_get_caps() in core_service.cc.
// Add entries here when new capabilities are added to the descriptor tables.
// ---------------------------------------------------------------------------

struct CapIdMapping {
    const char* name;
    uint16_t    cap_id;
    bool        is_hw;   // true → CAP_FLAG_HARDWARE set in response
};

// Complete mapping table.  Ordering does not matter — cap_id is the stable key.
static constexpr CapIdMapping kCapIdMappings[] = {
    { "bus.msx",           CAP_BUS_MSX,           true  },
    { "storage.ext_flash", CAP_STORAGE_EXT_FLASH, true  },
    { "storage.mass",      CAP_STORAGE_MASS,      false },
    { "storage.floppy",    CAP_STORAGE_FLOPPY,    false },
    { "net.wifi",          CAP_NET_WIFI,           true  },
    { "net.eth",           CAP_NET_ETH,            true  },
    { "net.esp32",         CAP_NET_ESP32,          false },
    { "io.usb_host",       CAP_IO_USB_HOST,        true  },
    { "io.adc",            CAP_IO_ADC,             true  },
    { "io.rs232",          CAP_IO_RS232,           true  },
    { "ui.oled",           CAP_UI_OLED,            true  },
    { "ui.eink",           CAP_UI_EINK,            true  },
    { "video.crt",         CAP_VIDEO_CRT,          true  },
    { "video.v9990",       CAP_VIDEO_V9990,        false },
    { "audio.out",         CAP_AUDIO_OUT,          true  },
    { "audio.opl4",        CAP_AUDIO_OPL4,         false },
    { "api.core",          CAP_API_CORE,           false },
    { "sw.mapper",         CAP_SW_MAPPER,          false },
    { "sw.psg",            CAP_SW_PSG,             false },
    { "sw.scc",            CAP_SW_SCC,             false },
    { "sw.menu",           CAP_SW_MENU,            false },
};
static constexpr size_t kCapIdMappingCount =
    sizeof(kCapIdMappings) / sizeof(kCapIdMappings[0]);

// Resolve a capability string name to its stable numeric ID.
// Returns 0x0000 (reserved/unknown) if the name is not in the table.
inline uint16_t cap_id_from_name(const char* name)
{
    for (size_t i = 0; i < kCapIdMappingCount; ++i) {
        const char* a = kCapIdMappings[i].name;
        const char* b = name;
        while (*a && *a == *b) { ++a; ++b; }
        if (*a == '\0' && *b == '\0') return kCapIdMappings[i].cap_id;
    }
    return 0x0000u; // unknown
}

// Return the is_hw flag for a capability name.  False if not found.
inline bool cap_is_hw(const char* name)
{
    for (size_t i = 0; i < kCapIdMappingCount; ++i) {
        const char* a = kCapIdMappings[i].name;
        const char* b = name;
        while (*a && *a == *b) { ++a; ++b; }
        if (*a == '\0' && *b == '\0') return kCapIdMappings[i].is_hw;
    }
    return false;
}

// System.GET_CAPS (0x02) — one entry per capability
// cap_id:    stable numeric ID from kCapIdMappings (doc/api/capabilities.md)
// cap_flags: CAP_FLAG_* bitmap
// cap_param: capability-specific parameter (0 for most; KB for storage)
struct CapEntry {
    uint16_t cap_id;
    uint16_t cap_flags;
    uint32_t cap_param;
};
static_assert(sizeof(CapEntry) == 8, "CapEntry must be 8 bytes");

// System.GET_SECURITY_INFO (0x05) response  (spec.md)
struct SecurityInfoResp {
    uint32_t posture_props;        // API_POSTURE_* bitfield
    uint8_t  boot_key_valid_mask;
    uint8_t  reserved0[3];
};
static_assert(sizeof(SecurityInfoResp) == 8, "SecurityInfoResp must be 8 bytes");

// System.GET_POLICY_FLAGS (0x06) response  (spec.md)
struct PolicyFlagsResp {
    uint32_t policy_flags;       // PolicyFlags bitfield (spec.md "Policy flags bit assignments v1")
    uint32_t policy_gen;         // generation counter (0 until KV store in Stage 6)
    uint8_t  policy_hash16[16];  // first 16 bytes of SHA-256(canonical policy bytes)
};
static_assert(sizeof(PolicyFlagsResp) == 24, "PolicyFlagsResp must be 24 bytes");

#pragma pack(pop)

# test_menu_bus.tcl — openMSX TCL test: menu page + API window bus mapping.
#
# Stage 13 test: verifies the bus-layer data layout for the menu page and
# API window regions that the RP2350 firmware maps into MSX page 1 / page 2.
#
# Machine: msx1_jlpicart_bus (slot 1 subslotted: subslot 1 = Menu Page RAM,
#          subslot 2 = API Window RAM).
#
# Invoke via run_test.sh with the bus fixture:
#   TEST_MACHINE=msx1_jlpicart_bus \
#     bash fw/tests/openMSX/run_test.sh fw/tests/openMSX/test_menu_bus.tcl
#
# Output:
#   "TESTRESULT: PASS" on success  (exit 0)
#   "TESTRESULT: FAIL <reason>"   (exit 1)
#
# --- Memory regions under test ---
# Menu Page  (subslot 1, 0x4000–0x7FFF, RW):
#   0x4000  MenuStubHeader.sig[4]  "JLMN"
#   0x4004  MenuStubHeader.abi_major  == 1
#   0x4040  MenuMailboxRegs base      (RW — mailbox)
# API Window (subslot 2, 0x8000–0xBFFF, RO from Z80):
#   0x8000  ApiWindowHeader.sig[4]  "JLP1"
#
# This test loads synthetic headers directly into the debug RAM devices
# (bypassing slot selection) to simulate what RP2350 firmware writes before
# BUS::start().  Slot routing itself is verified on hardware.

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

catch { renderer none }

# Device names matching msx1_jlpicart_bus.xml
set ::MENU_DEV  "Menu Page RAM"
set ::API_DEV   "API Window RAM"

# Page-relative offsets (device offset = Z80 address − page base)
set ::MENU_HDR_OFS      0x0000   ;# MenuStubHeader at start of menu page
set ::MENU_MBX_OFS      0x0040   ;# MenuMailboxRegs (Z80 0x4040 − 0x4000)
set ::API_HDR_OFS       0x0000   ;# ApiWindowHeader at start of API window

# ---------------------------------------------------------------------------
# Test framework
# ---------------------------------------------------------------------------

proc pass {} {
    puts stderr "TESTRESULT: PASS"
    exit 0
}

proc fail {reason} {
    puts stderr "TESTRESULT: FAIL $reason"
    exit 1
}

proc check_eq {label got expected} {
    if {$got != $expected} {
        fail [format "%s: got 0x%02X, expected 0x%02X" $label $got $expected]
    }
}

proc check_str {label dev ofs expected_chars} {
    set i 0
    foreach ch $expected_chars {
        set got [debug read $dev [expr {$ofs + $i}]]
        set exp [scan $ch %c]
        check_eq "$label\[${i}\]" $got $exp
        incr i
    }
}

# ---------------------------------------------------------------------------
# Step 1: write synthetic menu page header
# ---------------------------------------------------------------------------
#
# Simulate what RP2350 MenuMailbox::init() writes:
#   sig[4]      = "JLMN"
#   abi_major   = 1 (MENU_ABI_MAJOR)
#   abi_minor   = 0
#   header_len  = 64 (LE16)
#   data_ofs    = 0x0100 (LE16)
#
proc step_write_menu_header {} {
    set dev   $::MENU_DEV
    set base  $::MENU_HDR_OFS

    # sig "JLMN"
    debug write $dev [expr {$base + 0}] 0x4A   ;# 'J'
    debug write $dev [expr {$base + 1}] 0x4C   ;# 'L'
    debug write $dev [expr {$base + 2}] 0x4D   ;# 'M'
    debug write $dev [expr {$base + 3}] 0x4E   ;# 'N'
    # abi_major = 1
    debug write $dev [expr {$base + 4}] 0x01
    # abi_minor = 0
    debug write $dev [expr {$base + 5}] 0x00
    # reserved = 0
    debug write $dev [expr {$base + 6}] 0x00
    debug write $dev [expr {$base + 7}] 0x00
    # header_len = 64 (LE16)
    debug write $dev [expr {$base + 8}]  0x40
    debug write $dev [expr {$base + 9}]  0x00
    # data_ofs = 0x0100 (LE16)
    debug write $dev [expr {$base + 10}] 0x00
    debug write $dev [expr {$base + 11}] 0x01
}

# ---------------------------------------------------------------------------
# Step 2: write synthetic API window header
# ---------------------------------------------------------------------------
#
# Simulate what RP2350 ApiWindow::init() writes:
#   sig[4] = "JLP1"
#
proc step_write_api_header {} {
    set dev   $::API_DEV
    set base  $::API_HDR_OFS

    # sig "JLP1"
    debug write $dev [expr {$base + 0}] 0x4A   ;# 'J'
    debug write $dev [expr {$base + 1}] 0x4C   ;# 'L'
    debug write $dev [expr {$base + 2}] 0x50   ;# 'P'
    debug write $dev [expr {$base + 3}] 0x31   ;# '1'
}

# ---------------------------------------------------------------------------
# Step 3: verify menu page header
# ---------------------------------------------------------------------------

proc step_verify_menu_header {} {
    set dev  $::MENU_DEV
    set base $::MENU_HDR_OFS

    check_str "menu sig" $dev $base {J L M N}

    set abi_major [debug read $dev [expr {$base + 4}]]
    check_eq "menu abi_major" $abi_major 1

    set hdr_len_lo [debug read $dev [expr {$base + 8}]]
    set hdr_len_hi [debug read $dev [expr {$base + 9}]]
    set hdr_len [expr {$hdr_len_lo | ($hdr_len_hi << 8)}]
    check_eq "menu header_len" $hdr_len 64

    set data_ofs_lo [debug read $dev [expr {$base + 10}]]
    set data_ofs_hi [debug read $dev [expr {$base + 11}]]
    set data_ofs [expr {$data_ofs_lo | ($data_ofs_hi << 8)}]
    check_eq "menu data_ofs" $data_ofs 0x0100
}

# ---------------------------------------------------------------------------
# Step 4: verify API window header
# ---------------------------------------------------------------------------

proc step_verify_api_header {} {
    set dev  $::API_DEV
    set base $::API_HDR_OFS

    check_str "api sig" $dev $base {J L P 1}
}

# ---------------------------------------------------------------------------
# Step 5: verify mailbox area is read-write
# ---------------------------------------------------------------------------
#
# Write a sentinel byte to the mailbox base (offset 0x40 within menu page),
# read it back, and verify the round-trip.

proc step_verify_mailbox_rw {} {
    set dev    $::MENU_DEV
    set ofs    $::MENU_MBX_OFS
    set sentinel 0xA5

    debug write $dev $ofs $sentinel
    set got [debug read $dev $ofs]
    check_eq "mailbox RW" $got $sentinel

    # Clean up: restore to 0 (cmd_seq initial value)
    debug write $dev $ofs 0x00
    debug write $dev [expr {$ofs + 1}] 0x00
}

# ---------------------------------------------------------------------------
# Main: schedule tests after power-on settles (0.1 s machine time is enough
# since we only use debug device commands — no Z80 execution needed).
# ---------------------------------------------------------------------------

proc run_all_tests {} {
    step_write_menu_header
    step_write_api_header
    step_verify_menu_header
    step_verify_api_header
    step_verify_mailbox_rw
    pass
}

after time 0.1 run_all_tests

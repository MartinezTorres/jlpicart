# test_menu_stub_basic.tcl — openMSX TCL test: menu stub mailbox protocol.
#
# Verifies that the Z80 menu stub correctly handles all required commands
# over the mailbox ABI.
#
# Invoke via run_test.sh:
#   bash fw/tests/openMSX/run_test.sh test_menu_stub_basic.tcl
#
# Output:
#   "TESTRESULT: PASS" on success  (exit 0)
#   "TESTRESULT: FAIL <reason>"   (exit 1)
#
# --- Memory map (Z80 view, page mapped at 0x4000) ---
# 0x4000  MenuStubHeader   (64 bytes)
# 0x4040  MenuMailboxRegs  (64 bytes)
# 0x4100  data buffer      (command payload in/out)
# 0x7800  stub code

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

# Disable rendering for headless / CI operation.
catch { renderer none }

# Z80 addresses.
set ::MBX_BASE     0x4040
set ::MBX_CMD_SEQ  [expr {$::MBX_BASE +  0}]  ;# uint16_t
set ::MBX_RESP_SEQ [expr {$::MBX_BASE +  2}]  ;# uint16_t
set ::MBX_CMD_ID   [expr {$::MBX_BASE +  4}]  ;# uint16_t
set ::MBX_STATUS   [expr {$::MBX_BASE +  6}]  ;# uint16_t
set ::MBX_ARG0     [expr {$::MBX_BASE +  8}]  ;# uint16_t
set ::MBX_IN_LEN   [expr {$::MBX_BASE + 24}]  ;# uint16_t
set ::MBX_OUT_LEN  [expr {$::MBX_BASE + 26}]  ;# uint16_t
set ::DATA_BUF      0x4100

# Command IDs (matches stub.c / spec §8).
set ::CMD_NOP           0x0000
set ::CMD_GET_HOST_INFO 0x0001
set ::CMD_SET_MODE      0x0002
set ::CMD_CLEAR         0x0003
set ::CMD_PUT_TEXT      0x0004
set ::CMD_READ_INPUT    0x0005
set ::CMD_IDLE          0x000A

# Status codes.
set ::MENU_OK            0x0000
set ::MENU_E_UNSUPPORTED 0x0001

# Current command sequence number (starts at 0 after stub init).
set ::cmd_seq 0

# ---------------------------------------------------------------------------
# Memory helpers
# ---------------------------------------------------------------------------

proc read_word {addr} {
    set lo [debug read memory $addr]
    set hi [debug read memory [expr {$addr + 1}]]
    return [expr {$lo | ($hi << 8)}]
}

proc write_word {addr val} {
    debug write memory $addr             [expr {$val & 0xFF}]
    debug write memory [expr {$addr + 1}] [expr {($val >> 8) & 0xFF}]
}

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
        fail "$label: got $got, expected $expected"
    }
}

# Send a command to the stub:
#   cmd_id  — command ID
#   arg0    — optional 16-bit argument (default 0)
#   in_str  — optional ASCII payload string written to DATA_BUF
# Writes fields in the correct order (data/cmd_id/arg0/in_len before cmd_seq).
proc mbx_send {cmd_id {arg0 0} {in_str ""}} {
    write_word $::MBX_CMD_ID $cmd_id
    write_word $::MBX_ARG0   $arg0

    set in_len 0
    if {$in_str ne ""} {
        binary scan $in_str c* bytes
        set i 0
        foreach b $bytes {
            # c* gives signed values; mask to unsigned byte.
            debug write memory [expr {$::DATA_BUF + $i}] [expr {$b & 0xFF}]
            incr i
        }
        set in_len $i
    }
    write_word $::MBX_IN_LEN $in_len

    # Write cmd_seq last to signal the new command (Z80 spin-waits on this).
    incr ::cmd_seq
    write_word $::MBX_CMD_SEQ [expr {$::cmd_seq & 0xFFFF}]
}

# ---------------------------------------------------------------------------
# Test steps
# ---------------------------------------------------------------------------

# Step 0: wait 2 s for C-BIOS to fully initialise hardware (VDP, PSG, etc.).
# C-BIOS treats slot 1 as an internal device and does not call the cartridge
# INIT vector there.  Instead we inject a 7-byte bootstrap into the 64 KB
# Main RAM (slot 3) and redirect the Z80 PC to it.
#
# Bootstrap at Z80 0xFFF0 (page 3, slot 3 = Main RAM, offset 0x3FF0):
#   3E C4        LD A, 0xC4      ; page3=slot3, page2=slot0, page1=slot1, page0=slot0
#   D3 A8        OUT (0xA8), A   ; write PPI slot-select: page 1 → slot 1
#   C3 lo hi     JP  entry       ; jump to __start (DI + LD SP + CALL _main)
#
# The entry point is read from the ROM header bytes 2-3 (written by the
# Makefile from stub.map __start).  This keeps the bootstrap correct even if
# stub code size changes and __start moves.
#
# After the OUT the Z80 address space has our Stub Test RAM at 0x4000-0x7FFF.
proc step_wait_init {} {
    # Sanity: verify ROM header is in Stub Test RAM backing store.
    set b0 [debug read "Stub Test RAM" 0]
    set b1 [debug read "Stub Test RAM" 1]
    if {$b0 != 0x41 || $b1 != 0x42} {
        fail [format "ROM not in Stub Test RAM: \[0\]=0x%02X \[1\]=0x%02X" $b0 $b1]
    }

    # Read __start entry point from ROM header bytes 2-3.
    set entry_lo [debug read "Stub Test RAM" 2]
    set entry_hi [debug read "Stub Test RAM" 3]

    # Write 7-byte bootstrap to Main RAM offset 0x3FF0 (Z80 page 3 = 0xFFF0).
    set boot [list 0x3E 0xC4 0xD3 0xA8 0xC3 $entry_lo $entry_hi]
    for {set i 0} {$i < [llength $boot]} {incr i} {
        debug write "Main RAM" [expr {0x3FF0 + $i}] [lindex $boot $i]
    }

    # Redirect Z80 PC to the bootstrap (PCL=0xF0, PCH=0xFF).
    debug write "CPU regs" 12 0xF0
    debug write "CPU regs" 13 0xFF

    # Wait 0.5 s machine time for stub to complete its init sequence.
    after time 0.5 step_check_stub_init
}

# Step 1: verify stub completed init (wrote host_caps), then start commands.
proc step_check_stub_init {} {
    # Stub init writes HDR_HOST_CAPS_LO at {Stub Test RAM}[0x10] = Z80 0x4010.
    # If still 0x00, stub never ran.
    set caps [debug read "Stub Test RAM" 0x10]
    if {$caps == 0} {
        fail "stub init timed out after 0.5 s (caps@0x4010 still 0)"
    }

    # Verify resp_seq == cmd_seq (both 0 — no command sent yet).
    set cs [read_word $::MBX_CMD_SEQ]
    set rs [read_word $::MBX_RESP_SEQ]
    if {$cs != $rs} {
        fail "stub init: cmd_seq=$cs resp_seq=$rs mismatch"
    }

    # Proceed to GET_HOST_INFO.
    mbx_send $::CMD_GET_HOST_INFO
    after time 0.1 step_check_get_host_info
}

proc step_check_get_host_info {} {
    set rs [read_word $::MBX_RESP_SEQ]
    if {$rs != $::cmd_seq} {
        fail "GET_HOST_INFO: no response (resp_seq=$rs cmd_seq=$::cmd_seq)"
    }
    set status  [read_word $::MBX_STATUS]
    set out_len [read_word $::MBX_OUT_LEN]
    check_eq "GET_HOST_INFO status"  $status  $::MENU_OK
    check_eq "GET_HOST_INFO out_len" $out_len 12

    # HostInfo layout: msx_gen(1B) vram_kb(1B) text_cols(1B) reserved(1B) ...
    set msx_gen   [debug read memory $::DATA_BUF]
    set text_cols [debug read memory [expr {$::DATA_BUF + 2}]]
    if {$msx_gen < 1 || $msx_gen > 4} {
        fail "GET_HOST_INFO: msx_gen=$msx_gen out of range 1..4"
    }
    check_eq "GET_HOST_INFO text_cols" $text_cols 40

    # Proceed: SET_MODE screen 0 (text mode).
    mbx_send $::CMD_SET_MODE 0
    after time 0.1 step_check_set_mode
}

proc step_check_set_mode {} {
    set rs [read_word $::MBX_RESP_SEQ]
    if {$rs != $::cmd_seq} { fail "SET_MODE: no response" }
    check_eq "SET_MODE status" [read_word $::MBX_STATUS] $::MENU_OK

    # Proceed: CLEAR.
    mbx_send $::CMD_CLEAR
    after time 0.1 step_check_clear
}

proc step_check_clear {} {
    set rs [read_word $::MBX_RESP_SEQ]
    if {$rs != $::cmd_seq} { fail "CLEAR: no response" }
    check_eq "CLEAR status" [read_word $::MBX_STATUS] $::MENU_OK

    # Proceed: PUT_TEXT "JLP" at row=0, col=0.
    # arg0 = (row << 8) | col = 0x0000.
    mbx_send $::CMD_PUT_TEXT 0x0000 "JLP"
    after time 0.1 step_check_put_text
}

proc step_check_put_text {} {
    set rs [read_word $::MBX_RESP_SEQ]
    if {$rs != $::cmd_seq} { fail "PUT_TEXT: no response" }
    check_eq "PUT_TEXT status" [read_word $::MBX_STATUS] $::MENU_OK

    # Proceed: READ_INPUT.
    mbx_send $::CMD_READ_INPUT
    after time 0.1 step_check_read_input
}

proc step_check_read_input {} {
    set rs [read_word $::MBX_RESP_SEQ]
    if {$rs != $::cmd_seq} { fail "READ_INPUT: no response" }
    set status  [read_word $::MBX_STATUS]
    set out_len [read_word $::MBX_OUT_LEN]
    check_eq "READ_INPUT status"  $status  $::MENU_OK
    check_eq "READ_INPUT out_len" $out_len 16

    # Proceed: unknown command (0xFFFF) → expect E_UNSUPPORTED.
    mbx_send 0xFFFF
    after time 0.1 step_check_unknown_cmd
}

proc step_check_unknown_cmd {} {
    set rs [read_word $::MBX_RESP_SEQ]
    if {$rs != $::cmd_seq} { fail "unknown cmd: no response" }
    check_eq "unknown cmd status" [read_word $::MBX_STATUS] $::MENU_E_UNSUPPORTED

    pass
}

# ---------------------------------------------------------------------------
# Main: schedule first step after 2 s machine time.
# No vwait needed — the openMSX scheduler fires after time callbacks from
# its main loop; vwait blocks the control handler and prevents firing.
# exit 0/1 in pass/fail terminates the process when the test completes.
# ---------------------------------------------------------------------------

after time 2.0 step_wait_init

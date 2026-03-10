// menu_host_abi.cc — RP2350-side Menu Host ABI controller.
//
// Implements the RP2350 half of the command/response protocol.
// The Z80 stub (fw/z80/menu_stub/) implements the other half.
//
// Spec reference: spec.md §8 (Menu Host ABI).

#include "msx/menu/menu_host_abi.h"

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

void MenuMailbox::init(uint8_t* page, uint16_t stub_entry)
{
    memset(page, 0, MENU_PAGE_SIZE);

    hdr_  = reinterpret_cast<MenuStubHeader*>(page + MENU_HEADER_OFS);
    mbx_  = reinterpret_cast<MenuMailboxRegs*>(page + MENU_MAILBOX_OFS);
    data_ = page + MENU_DATA_OFS;

    // Write MenuStubHeader.
    hdr_->sig[0]      = 'J'; hdr_->sig[1] = 'L';
    hdr_->sig[2]      = 'M'; hdr_->sig[3] = 'N';
    hdr_->abi_major   = MENU_ABI_MAJOR;
    hdr_->abi_minor   = MENU_ABI_MINOR;
    hdr_->header_len  = sizeof(MenuStubHeader);
    hdr_->mailbox_ofs = MENU_MAILBOX_OFS;
    hdr_->data_ofs    = MENU_DATA_OFS;
    hdr_->data_len    = MENU_DATA_LEN;
    hdr_->stub_entry  = stub_entry;
    // host_caps and vdp_caps are 0 until the Z80 stub fills them in.

    // Mailbox starts idle: cmd_seq == resp_seq == 0, no pending command.
    mbx_->cmd_seq  = 0;
    mbx_->resp_seq = 0;

    pending_      = false;
    last_status_  = 0;
    last_out_len_ = 0;
    initialized_  = true;
}

// ---------------------------------------------------------------------------
// send_command
// ---------------------------------------------------------------------------

bool MenuMailbox::send_command(uint16_t cmd_id,
                               uint32_t arg0, uint32_t arg1,
                               uint32_t arg2, uint32_t arg3,
                               const uint8_t* in_data, uint16_t in_len)
{
    if (!initialized_ || pending_) {
        return false;
    }

    if (in_len > MENU_DATA_LEN) {
        return false; // would overflow the shared data buffer
    }

    // Write all command fields before advancing cmd_seq (spec §8 ordering rule).
    mbx_->cmd_id = cmd_id;
    mbx_->arg0   = arg0;
    mbx_->arg1   = arg1;
    mbx_->arg2   = arg2;
    mbx_->arg3   = arg3;
    mbx_->status = 0;
    mbx_->out_len= 0;
    mbx_->in_len = in_len;

    if (in_len > 0 && in_data != nullptr) {
        memcpy(data_, in_data, in_len);
    }

    // Signal the stub: write cmd_seq last.
    // DMB ensures all preceding field writes are visible before the sequence
    // number update.  On host tests a compiler barrier suffices.
#ifndef JLPICART_HOST_TEST
    __asm__ volatile ("dmb" ::: "memory");
#else
    __asm__ volatile ("" ::: "memory");
#endif
    mbx_->cmd_seq = static_cast<uint16_t>(mbx_->cmd_seq + 1u);

    pending_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// tick
// ---------------------------------------------------------------------------

bool MenuMailbox::tick()
{
    if (!initialized_ || !pending_) {
        return false;
    }

    // The stub signals completion by writing resp_seq = cmd_seq (last).
    if (mbx_->resp_seq != mbx_->cmd_seq) {
        return false; // still waiting
    }

    // DMB: ensure resp_seq observation is ordered before reading status/out_len.
#ifndef JLPICART_HOST_TEST
    __asm__ volatile ("dmb" ::: "memory");
#else
    __asm__ volatile ("" ::: "memory");
#endif
    last_status_  = mbx_->status;
    last_out_len_ = mbx_->out_len;
    pending_      = false;
    return true;
}

#pragma once
#include <cstddef>

// board_descriptor.h — hardware capability declarations for JLPiCart.
//
// A BoardCapabilityDecl says "this board has hardware X".
// Declared ≠ present ≠ active. No probing happens in Stage 3.
// Probing is gated on: declared + allowed + requested + safe_verify=true.
// See spec.md §5.1 (Resource and capability model contract v1).

struct BoardCapabilityDecl {
    const char* name;         // stable capability name (e.g. "hw.wifi")
    bool        safe_verify;  // true = firmware MAY probe this HW to verify presence
};

struct BoardDescriptor {
    const char*                  board_id;       // e.g. "jlpicart_v1"
    const BoardCapabilityDecl*   capabilities;
    size_t                       capability_count;

    // Returns the descriptor for the board this firmware was built for.
    // There is exactly one implementation of this function per board target
    // (fw/src/boards/board_descriptor_jlpicart.cc).
    static const BoardDescriptor& for_current_board();
};

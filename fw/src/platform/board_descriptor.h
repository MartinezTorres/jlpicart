#pragma once
#include <cstddef>
#include "allocator/resource_model.h"

// board_descriptor.h — hardware capability declarations for JLPiCart.
//
// A BoardCapabilityDecl says "this board has hardware X".
// Declared ≠ present ≠ active.
// Probing is gated on: declared + allowed + requested + safe_verify=true.

struct BoardCapabilityDecl {
    const char*          name;         // stable capability name (e.g. "bus.msx")
    bool                 safe_verify;  // true = firmware MAY probe this HW
    ResourceRequirements resources = {};  // budget consumed when active (zero = none)
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

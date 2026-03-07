// JLPiCart firmware
//
// Stage 1 placeholder — compiles and boots; does nothing else.
// Subsequent stages add: spine init (diag, log, security posture, policy,
// capability registry), API window, menu ABI, storage substrate, and
// collection loading.
//
// See fw/bootstrapping_v8.md for the staged implementation plan.
// See fw/spec_v22.md for the platform specification.

#include "pico/stdlib.h"

int main() {
    // Nothing runs here yet. Spine modules will be initialized here
    // from Stage 3 onwards, in the order defined in bootstrapping_v8.md.
    while (true) {
        tight_loop_contents();
    }
}

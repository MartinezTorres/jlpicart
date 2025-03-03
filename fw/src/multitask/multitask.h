#pragma once

#include <board.h>

namespace Multitask {

    enum CallAgain : bool { DO_NOT_CALL_AGAIN = false, CALL_AGAIN = true };

    using Task = CallAgain(*)();
    
    void add_task(Task task);

    void clear_tasks();

    void init();
}

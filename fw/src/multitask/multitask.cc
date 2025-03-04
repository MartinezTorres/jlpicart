#include <multitask/multitask.h>

namespace Multitask {

    queue_t taskQueue;
    
    volatile bool clearRequested = false;
    volatile bool core1Idle = true;

    static void __no_inline_not_in_flash_func(core1_task_runner)() {
        DBG::msg<DEBUG_INFO>("  Multitask: start.");
        Task task;
        while (true) {
            core1Idle = true; // Mark core1 as idle when waiting for tasks
            tight_loop_contents(); // Yield execution without unnecessary delay
            
            while (queue_try_remove(&taskQueue, &task)) {
                core1Idle = false; // Mark core1 as busy
                if (clearRequested) continue; // Exit loop safely if clearing is requested
                
                if (task() == CALL_AGAIN) { // If the task returns true, re-queue it
                    if (!queue_try_add(&taskQueue, &task)) {
                        DBG::msg<DEBUG_INFO>("\n Multitask: error in queueing task.");
                    }
                }
            }
        }
    }
    
    void add_task(Task task) {
        while (!queue_try_add(&taskQueue, &task)) {
            tight_loop_contents(); // Retry until the queue has space
        }
    }

    void clear_tasks() {
        clearRequested = true; // Signal core1 to stop re-queuing tasks
        while (!core1Idle) {
            tight_loop_contents(); // Wait for Core 1 to finish its current task
        }
        __sync_synchronize(); // Memory barrier to ensure consistency
        clearRequested = false; // Allow new tasks to be added
    }

    void init() {
        DBG::msg<DEBUG_INFO>("Multitask initialization");

        queue_init(&taskQueue, sizeof(Task), 32); // Ensure queue is initialized before launching Core 1
        multicore_launch_core1(core1_task_runner);
        
        busy_wait_us(50 * 1000);
    }
}

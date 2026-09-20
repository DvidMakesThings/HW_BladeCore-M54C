/**
 * @file src/Software/BaReTOS/incl/Tasks/example_task.c
 *
 * @version 1.0.0
 * @date 2026-09-18
 *
 * @details
 * Reference task showing how to add a new application task to BaReTOS. It does no real work; it
 * exists as a copy-paste starting point for anyone new to FreeRTOS.
 *
 * What it demonstrates:
 *   - A task is just a function that never returns: it loops forever.
 *   - Every iteration must block (here vTaskDelayUntil). Blocking hands the CPU back to the
 *     scheduler so lower-priority tasks and the idle task can run. A task that never blocks
 *     starves everything below it.
 *   - vTaskDelayUntil gives a stable period; the wake time is absolute, not "now + delay".
 *
 * -----------------------------------------------------------------------------------------------
 * How to add a new task (this file is already wired in as the worked example):
 *
 *   Step                                   This example                     File
 *   -------------------------------------  -------------------------------  ----------------------
 *   1. Set priority, stack, and timing     EXAMPLE_TASK_PRIORITY (idle+1),  CONFIG.h
 *                                          EXAMPLE_PERIOD_MS,
 *                                          EXAMPLE_STACK_WORDS
 *   2. Declare the entry point             example_task() incl/Tasks/application.h
 *   3. Write the task body                 this file incl/Tasks/example_task.c
 *   4. Create it before the scheduler      xTaskCreate(..., configASSERT)   main.c
 *   5. Add the source to the build         incl/Tasks/example_task.c        CMakeLists.txt
 *                                                                           (USER SOURCES block)
 *
 * To start your own task, copy this file, rename example_task, and repeat the five steps with
 * your own MYTASK_* names.
 * -----------------------------------------------------------------------------------------------
 *
 * @project BaReTOS - BladeCore-M54C firmware
 * @github https://github.com/DvidMakesThings/HW_BladeCore-M54C
 */
#include "application.h"
#include "pico/stdlib.h"

/**
 * @brief Periodic example task used as a template for new application tasks.
 *
 * @param[in] parameter Unused FreeRTOS task argument; pass NULL.
 * @return None (void); this task never returns.
 *
 * @details
 * Wakes every EXAMPLE_PERIOD_MS on an absolute schedule and does nothing else.
 *
 * The pattern to reuse:
 *   1. Set up any state once, before the loop (here: the wake-time reference).
 *   2. Loop forever.
 *   3. Do the per-iteration work.
 *   4. Block until the next wake so the CPU is free for other tasks.
 *
 * For event-driven work instead of a fixed period, drop the delay and block on ulTaskNotifyTake,
 * and wake the task from an ISR with vTaskNotifyGiveFromISR (see can_task.c).
 */
void example_task(void *parameter)
{
    TickType_t wake_time = xTaskGetTickCount(); /**< Absolute periodic schedule reference. */
    (void)parameter;

    for (;;)
    {
        /* Step 3: add per-iteration work here. Keep it short and non-blocking; long or
         * busy-waiting work at this priority would delay lower-priority tasks. */

        /* Step 4: block until the next period so other tasks can run. */
        vTaskDelayUntil(&wake_time, pdMS_TO_TICKS(EXAMPLE_PERIOD_MS));
    }
}

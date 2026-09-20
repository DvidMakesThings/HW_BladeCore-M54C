/**
 * @file src/Software/BaReTOS/incl/Tasks/heartbeat.c
 *
 * @version 1.0.0
 * @date 2026-09-18
 *
 * @details
 * Implements the minimum-priority heartbeat task. The LED changes state on a fixed half-second
 * schedule, and watchdog feeding requires fresh progress from the CAN task. A stalled or
 * monopolising higher-priority task therefore cannot be hidden by an independently fed
 * watchdog. The task blocks on every iteration so the scheduler idle task remains runnable.
 *
 * @project BaReTOS - BladeCore-M54C firmware
 * @github https://github.com/DvidMakesThings/HW_BladeCore-M54C
 */
#include "application.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"

/**
 * @brief Toggle the heartbeat at the minimum scheduler priority.
 *
 * @param[in] parameter Unused FreeRTOS task argument; pass NULL.
 * @return None (void); this task never returns.
 *
 * @details
 * Toggles the board heartbeat LED every HEARTBEAT_PERIOD_MS using an absolute FreeRTOS delay
 * schedule. The task runs at tskIDLE_PRIORITY, the minimum scheduler priority. It feeds the
 * watchdog only after the CAN service counter has advanced, so a healthy heartbeat proves
 * progress in both tasks. Each iteration blocks to leave CPU time available to the idle task.
 */
void heartbeat_task(void *parameter)
{
    TickType_t wake_time = xTaskGetTickCount();      /**< Absolute periodic schedule reference. */
    uint32_t previous_cycles = bare_diag.can_cycles; /**< Last observed CAN progress. */
    (void)parameter;

    for (;;)
    {
        gpio_xor_mask64(1ull << PIN_HEARTBEAT);
        ++bare_diag.heartbeat;
        /* Feeding here requires progress from both the CAN and lowest-priority task. */
        if (bare_diag.can_cycles != previous_cycles)
        {
            watchdog_update();
        }
        previous_cycles = bare_diag.can_cycles;
        vTaskDelayUntil(&wake_time, pdMS_TO_TICKS(HEARTBEAT_PERIOD_MS));
    }
}

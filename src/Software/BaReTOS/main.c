/**
 * @file src/Software/BaReTOS/main.c
 *
 * @version 1.0.0
 * @date 2026-09-18
 *
 * @details
 * Starts the generated BaReTOS project with the two requested application tasks: Heartbeat at
 * the minimum scheduler priority and a higher-priority CAN listener. Initialises the board
 * clock and silicon identity before checking both task allocations and starting FreeRTOS. The
 * watchdog is serviced only when both tasks make progress. Fault hooks request reset rather
 * than continuing with an invalid scheduler state.
 *
 * @project BaReTOS - BladeCore-M54C firmware
 * @github https://github.com/DvidMakesThings/HW_BladeCore-M54C
 */
#include "CONFIG.h"
#include "application.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include <string.h>

volatile application_diagnostics_t bare_diag; /**< Runtime diagnostic state. */
TaskHandle_t can_task_handle;                 /**< Handle assigned before the scheduler starts. */

/**
 * @brief Reset following a failed runtime assertion.
 *
 * @param[in] file Source filename reporting the failure.
 * @param[in] line Source line reporting the failure.
 * @return None (void); this function never returns.
 *
 * @details
 * Disables task interrupts and requests a short watchdog reset instead of continuing with a
 * potentially corrupted scheduler state. The source location is supplied by configASSERT and
 * remains available to a debugger at the call site. The function then stays in a tight loop
 * until reset; it must never return to the failed operation.
 */
void vAssertCalled(const char *file, int line)
{
    (void)file;
    (void)line;
    taskDISABLE_INTERRUPTS();
    watchdog_reboot(0, 0, FAULT_RESET_DELAY_MS);
    while (true)
    {
        tight_loop_contents();
    }
}

/**
 * @brief Reset when FreeRTOS detects a task stack overflow.
 *
 * @param[in] task Task whose stack failed validation.
 * @param[in] name Name of the affected task.
 * @return None (void); delegates to the non-returning assertion handler.
 *
 * @details
 * Receives the task handle and name supplied by the FreeRTOS stack-overflow check. It forwards
 * the failure to the common assertion handler, which disables interrupts and resets the MCU.
 * No allocation, logging, or further task processing is attempted on a potentially corrupted
 * stack.
 */
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    vAssertCalled(name, 0);
}

/**
 * @brief Reset when the FreeRTOS heap cannot satisfy an allocation.
 *
 * @par Parameters
 * None (void).
 * @return None (void); delegates to the non-returning assertion handler.
 *
 * @details
 * Handles failure of a FreeRTOS dynamic allocation by entering the common reset path. The hook
 * avoids any additional heap use. Task creation in main is also checked explicitly so the
 * scheduler cannot start without both required tasks.
 */
void vApplicationMallocFailedHook(void)
{
    vAssertCalled("heap", 0);
}

/**
 * @brief Initialise board state, create both tasks, and start FreeRTOS.
 *
 * @par Parameters
 * None (void).
 * @return Does not return during normal operation; scheduler failure causes reset.
 *
 * @details
 * Initialises board clock, heartbeat GPIO, and the full silicon identifier before allocating
 * Heartbeat and CAN tasks. Both allocation results are checked. The scheduler starts only
 * after the watchdog is enabled, and any unexpected scheduler return enters the common fault-
 * reset path.
 */
int main(void)
{
    uint8_t identity[8]; /**< Full silicon identifier fetched before scheduling. */
    set_sys_clock_khz(SYSTEM_CLOCK_KHZ, true);
    gpio_init(PIN_HEARTBEAT);
    gpio_set_dir(PIN_HEARTBEAT, GPIO_OUT);
    bare_diag.node = board_identity(identity);
    memcpy((void *)bare_diag.uid, identity, sizeof(identity));

    /* Validate allocations before enabling the task-supervised watchdog. */
    configASSERT(xTaskCreate(init_task, "Init", INIT_STACK_WORDS, NULL, INIT_TASK_PRIORITY, NULL) ==
                 pdPASS);
    configASSERT(xTaskCreate(heartbeat_task, "Heartbeat", HEARTBEAT_STACK_WORDS, NULL,
                             HEARTBEAT_TASK_PRIORITY, NULL) == pdPASS);
    configASSERT(xTaskCreate(can_task, "CAN", CAN_STACK_WORDS, NULL, CAN_TASK_PRIORITY,
                             &can_task_handle) == pdPASS);
    configASSERT(xTaskCreate(example_task, "Example", EXAMPLE_STACK_WORDS, NULL,
                             EXAMPLE_TASK_PRIORITY, NULL) == pdPASS);
    watchdog_enable(WATCHDOG_TIMEOUT_MS, true);
    vTaskStartScheduler();
    vAssertCalled("scheduler", 0);
    return 0;
}

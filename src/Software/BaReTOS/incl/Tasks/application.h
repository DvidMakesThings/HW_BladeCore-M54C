/**
 * @file src/Software/BaReTOS/incl/Tasks/application.h
 *
 * @version 1.0.0
 * @date 2026-09-18
 *
 * @details
 * Declares the two RTOS task entry points and a weak application-level CAN receive hook. It
 * also defines debugger-visible health counters so commissioning can verify task progress,
 * controller loopback, frame reception, and error conditions without enabling USB or UART. The
 * CAN handle is published only for direct notification from its GPIO interrupt.
 *
 * @project BaReTOS - BladeCore-M54C firmware
 * @github https://github.com/DvidMakesThings/HW_BladeCore-M54C
 */
#pragma once
#include "board_can.h"
#include "FreeRTOS.h"
#include "task.h"

/** @brief Runtime health counters available over SWD. */
typedef struct
{
    uint32_t heartbeat;   /**< Number of heartbeat LED transitions. */
    uint32_t can_cycles;  /**< Number of completed CAN service iterations. */
    uint32_t received;    /**< Frames received in either identifier format. */
    uint32_t transmitted; /**< Successfully acknowledged transmissions. */
    uint32_t tx_errors;   /**< Failed or timed-out transmissions. */
    uint32_t overflows;   /**< Receive overflow events. */
    uint32_t node;        /**< Sixteen-bit silicon-derived node address. */
    uint32_t can_ready;   /**< Controller successfully entered normal mode. */
    uint32_t selftest_ok; /**< Internal MCP2515 loopback test result. */
    uint32_t last_id;     /**< Most recently received CAN identifier. */
    uint32_t last_error;  /**< Most recent MCP2515 EFLG value. */
    uint8_t uid[8];       /**< Full silicon identifier. */
    uint8_t last_data[8]; /**< Most recent received payload. */
    uint8_t can_regs[8]; /**< DEBUG: live MCP2515 CANSTAT,CANCTRL,EFLG,TEC,REC,CANINTF,RXB0,RXB1. */
} application_diagnostics_t;

extern volatile application_diagnostics_t bare_diag; /**< SWD-visible task health. */
extern TaskHandle_t can_task_handle; /**< Task notified by the controller interrupt. */

/**
 * @brief One-shot startup task: run all peripheral initialisation, then self-delete.
 *
 * @param[in] parameter Unused FreeRTOS task argument; pass NULL.
 * @return None (void); the task removes itself with vTaskDelete once init completes.
 *
 * @details
 * Runs at the highest priority so it finishes before the service tasks do real work. It powers
 * and calibrates the ADC, then deletes itself to free its stack. Add further one-time startup
 * steps here.
 */
void init_task(void *parameter);

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
void heartbeat_task(void *parameter);

/**
 * @brief Own SPI1 and service receive traffic and bootloader requests.
 *
 * @param[in] parameter Unused FreeRTOS task argument; pass NULL.
 * @return None (void); this task never returns.
 *
 * @details
 * Runs an internal MCP2515 loopback check, then retries normal controller initialisation
 * without preventing heartbeat execution. A GPIO interrupt only notifies this task; all SPI
 * accesses stay in task context. Each service pass handles a bounded receive budget, records
 * errors, and blocks at least one tick to prevent sustained CAN traffic from starving the
 * priority-zero heartbeat. A timed polling fallback handles missed or already-asserted
 * interrupt edges.
 */
void can_task(void *parameter);

/**
 * @brief Periodic example task used as a template for new application tasks.
 *
 * @param[in] parameter Unused FreeRTOS task argument; pass NULL.
 * @return None (void); this task never returns.
 *
 * @details
 * Wakes every EXAMPLE_PERIOD_MS on an absolute schedule and does nothing else. It exists as a
 * copy-paste starting point that demonstrates a non-returning task loop and periodic
 * vTaskDelayUntil scheduling. Replace the body with real work and adjust the EXAMPLE_* defines
 * in CONFIG.h. See example_task.c for the steps to add a new task.
 */
void example_task(void *parameter);

/**
 * @brief Application extension point for received CAN traffic.
 *
 * @param[in] frame Borrowed frame valid only during this call.
 * @return None (void).
 *
 * @details
 * Provides the deliberate extension point for future RTOS applications. The default weak
 * implementation ignores the borrowed frame. Override it with a strong definition in an incl
 * source to dispatch application messages, copying data into a queue when processing must
 * outlive the call. Blocking or long-running handlers would delay the CAN listener and can
 * trigger the task health watchdog.
 */
void application_can_received(const can_frame_t *frame);

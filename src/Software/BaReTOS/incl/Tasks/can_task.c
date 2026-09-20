/**
 * @file src/Software/BaReTOS/incl/Tasks/can_task.c
 *
 * @version 1.0.0
 * @date 2026-09-18
 *
 * @details
 * Owns MCP2515 SPI access in the RTOS application. It runs an internal loopback test,
 * initialises the bus, drains receive buffers, and forwards each frame to the message router in
 * can_router.c, which dispatches management, SDO, RPDO, and any future object types by class.
 * Receive work is bounded per pass and always followed by a blocking delay so the priority-zero
 * heartbeat can run under sustained traffic.
 *
 * @project BaReTOS - BladeCore-M54C firmware
 * @github https://github.com/DvidMakesThings/HW_BladeCore-M54C
 */
#include "application.h"
#include "can_router.h"
#include "pico/stdlib.h"
#include "hardware/irq.h"
#include <string.h>

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
__attribute__((weak)) void application_can_received(const can_frame_t *frame)
{
    /* Implement application-specific CAN dispatch here or override this weak hook. */
    (void)frame;
}

/**
 * @brief Wake the CAN task without using SPI from interrupt context.
 *
 * @param[in] gpio GPIO reporting a falling edge.
 * @param[in] events SDK interrupt event mask.
 * @return None (void).
 *
 * @details
 * Accepts only interrupts from PIN_CAN_INT and uses a FreeRTOS direct task notification to
 * wake the CAN owner. If a higher-priority task becomes runnable, the ISR requests a context
 * switch before exiting. No SPI transactions or frame decoding are performed in interrupt
 * context; the configured interrupt priority permits FreeRTOS FromISR calls.
 */
static void can_interrupt(uint gpio, uint32_t events)
{
    (void)events;
    if (gpio == PIN_CAN_INT && can_task_handle != NULL)
    {
        BaseType_t task_woken = pdFALSE; /**< Whether a higher-priority task was unblocked. */
        vTaskNotifyGiveFromISR(can_task_handle, &task_woken);
        portYIELD_FROM_ISR(task_woken);
    }
}

/**
 * @brief Record receive traffic and route the frame to its handler.
 *
 * @param[in] frame Frame consumed from the MCP2515.
 * @return None (void).
 *
 * @details
 * Records every received identifier and payload for diagnostics, calls the generic application
 * extension hook, then hands the frame to the message router. All per-message-type logic lives
 * in can_router.c, so new object types are added there without touching this task.
 */
static void handle_frame(const can_frame_t *frame)
{
    ++bare_diag.received;
    bare_diag.last_id = frame->id;
    memcpy((void *)bare_diag.last_data, frame->data, sizeof(frame->data));
    application_can_received(frame);
    can_router_dispatch(frame);
}

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
void can_task(void *parameter)
{
    (void)parameter;
    /* Initialise the controller exactly once, matching the working bootloader. */
    while (!(bare_diag.can_ready = board_can_init(false)))
    {
        /* Preserve heartbeat operation while reporting a missing controller over SWD. */
        ++bare_diag.can_cycles;
        vTaskDelay(pdMS_TO_TICKS(CAN_RETRY_MS));
    }
    irq_set_priority(IO_IRQ_BANK0, configMAX_SYSCALL_INTERRUPT_PRIORITY);
    gpio_set_irq_enabled_with_callback(PIN_CAN_INT, GPIO_IRQ_EDGE_FALL, true, can_interrupt);
    for (;;)
    {
        can_frame_t frame; /**< Next frame to dispatch. */
        /** @brief Number of receive buffers consumed in this service pass. */
        for (unsigned count = 0; count < CAN_RX_BUDGET && board_can_receive(&frame); ++count)
        {
            handle_frame(&frame);
        }
        bare_diag.last_error = board_can_errors();
        if (bare_diag.last_error & 0xc0)
        {
            ++bare_diag.overflows;
            board_can_clear_overflow();
        }
        ++bare_diag.can_cycles;

        /* Always block one tick so a saturated bus cannot starve heartbeat.
         * Polling also handles an IRQ that was already low before IRQ enabling. */
        vTaskDelay(pdMS_TO_TICKS(1));
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CAN_POLL_MS));
    }
}

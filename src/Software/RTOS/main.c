/*
 * main.c - BladeCore-M54C PCAN adapter firmware entry point (FreeRTOS)
 *
 * Startup sequence:
 *   1. Check for bootloader trigger (magic word from previous USB/CAN request)
 *   2. Initialize USB device task (TinyUSB + PCAN custom class)
 *   3. Initialize CAN bridge tasks (MCP2515 TX/RX via SPI)
 *   4. Initialize heartbeat LED task
 *   5. Start FreeRTOS scheduler
 */

#include "CONFIG.h"
#include "bootloader/bootloader.h"
#include "hal/can_bridge.h"
#include "usb/pcan_protocol.h"
#include "tusb.h"

#include "FreeRTOS.h"
#include "task.h"

#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/timer.h"

/* Watchdog timeout: 2 seconds.  Fed from the USB task every iteration.
 * If the firmware hangs (assert, deadlock, stack overflow), the watchdog
 * will reboot the device automatically instead of staying stuck. */
#define WATCHDOG_TIMEOUT_MS 2000

/* USB layer function (defined in pcan_usb_class.c) */
extern bool pcan_usb_send_rx_msg(const uint8_t *buf, uint16_t len);

/* -------------------------------------------------------------------------- */
/*  USB device task                                                            */
/* -------------------------------------------------------------------------- */
static void usb_device_task(void *param)
{
    (void)param;
    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO};
    tusb_init(0, &dev_init);

    /* Start watchdog now that the scheduler is running and we can feed it.
     * Must be after tusb_init so the USB enumeration window is covered. */
    watchdog_enable(WATCHDOG_TIMEOUT_MS, true);

    for (;;)
    {
        /* Process any pending USB events without blocking.
         * tud_task() blocks forever (starves CAN RX poll).
         * tud_task_ext(1, false) breaks EP1 OUT on RP2350.
         * Instead: check if events are ready, process them with
         * timeout=0, then do our periodic work and yield. */
        while (tud_task_event_ready())
        {
            tud_task_ext(0, false);
        }

        /* Reconfigure MCP2515 on bus-on transition */
        static bool s_was_bus_on = false;
        bool bus_on_now = pcan_is_bus_on();

        if (bus_on_now && !s_was_bus_on)
        {
            can_bridge_reconfigure();

            /* Send timesync event on EP2 IN.  This initializes the
             * host driver's internal timestamp reference.  The real
             * PCAN-USB firmware (and working STM32 clones) always
             * send a REC_TS + REC_ERROR pair on bus-on.  Without
             * this, the Windows PEAK driver silently discards all
             * subsequent data frames. */
            if (tud_ready())
            {
                uint8_t sb[PCAN_USB_EP_SIZE];
                uint64_t us = time_us_64();
                uint16_t ts = (uint16_t)((us * PCAN_TS_US_TO_TICK_NUM) /
                                         PCAN_TS_US_TO_TICK_DEN);
                pcan_encode_bus_active_msg(sb, ts);
                pcan_usb_send_rx_msg(sb, PCAN_USB_EP_SIZE);
            }
        }
        s_was_bus_on = bus_on_now;

        /* Forward CAN RX frames when USB is ready */
        if (tud_ready())
        {
            can_bridge_poll_rx();
        }

        vTaskDelay(pdMS_TO_TICKS(1));

        /* Feed watchdog -- if we ever hang, the device auto-reboots */
        watchdog_update();
    }
}

/* -------------------------------------------------------------------------- */
/*  Heartbeat LED task (breathing / slow blink)                                */
/* -------------------------------------------------------------------------- */
static void heartbeat_task(void *param)
{
    (void)param;

    gpio_init(PIN_HEARTBEAT);
    gpio_set_dir(PIN_HEARTBEAT, GPIO_OUT);

    bool led = false;
    for (;;)
    {
        led = !led;
        gpio_put(PIN_HEARTBEAT, led);

        /* Slow blink: 500ms on, 500ms off = normal operation
         * The blink rate visually distinguishes app mode from bootloader. */
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* -------------------------------------------------------------------------- */
/*  FreeRTOS hook implementations                                              */
/* -------------------------------------------------------------------------- */
void vAssertCalled(const char *file, int line)
{
    (void)file;
    (void)line;
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
        tight_loop_contents();
    }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    watchdog_reboot(0, 0, 100);
    for (;;)
    {
        tight_loop_contents();
    }
}

void vApplicationMallocFailedHook(void)
{
    watchdog_reboot(0, 0, 100);
    for (;;)
    {
        tight_loop_contents();
    }
}

/* -------------------------------------------------------------------------- */
/*  main                                                                       */
/* -------------------------------------------------------------------------- */
int main(void)
{
    /* Step 1: Check bootloader trigger (does not return if triggered) */
    bootloader_check();

    /* Step 2: Normal application startup */
    set_sys_clock_khz(150000, true);

    /* Step 3: Create tasks */
    xTaskCreate(usb_device_task, "usb", 2048, NULL, USB_TASK_PRIORITY, NULL);
    xTaskCreate(heartbeat_task, "hb", 256, NULL, HEARTBEAT_TASK_PRIORITY, NULL);
    can_bridge_create_tasks();

    /* Step 4: Start scheduler (does not return) */
    vTaskStartScheduler();

    for (;;)
    {
        tight_loop_contents();
    }
    return 0;
}

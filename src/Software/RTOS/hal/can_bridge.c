/*
 * can_bridge.c - CAN <-> USB bridge tasks (FreeRTOS)
 *
 * Tasks:
 *   can_tx_task  - Dequeues frames from tx_queue, sends via MCP2515
 *   can_rx_task  - Waits on MCP2515 interrupt, reads frames, queues for USB
 *
 * The USB task calls can_bridge_poll_rx() to drain the RX queue and
 * forward frames to the host via EP2 IN.
 */

#include "hal/can_bridge.h"
#include "CONFIG.h"
#include "drivers/mcp2515.h"
#include "usb/pcan_protocol.h"
#include "bootloader/bootloader.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/watchdog.h"
#include "hardware/irq.h"
#include <string.h>

/* -------------------------------------------------------------------------- */
/*  Configuration                                                              */
/* -------------------------------------------------------------------------- */
#define TX_QUEUE_DEPTH 32
#define RX_QUEUE_DEPTH 64
#define CAN_TX_STACK 1024
#define CAN_RX_STACK 1024

/* -------------------------------------------------------------------------- */
/*  Module state                                                               */
/* -------------------------------------------------------------------------- */
static mcp2515_t s_can_dev;
static QueueHandle_t s_tx_queue;
static QueueHandle_t s_rx_queue;
static SemaphoreHandle_t s_spi_mtx; /* protects SPI bus access */
static volatile bool s_bus_active;

/* -------------------------------------------------------------------------- */
/*  MCP2515 interrupt handler -- DISABLED                                      */
/*  Pico SDK GPIO ISR on RP2350 is incompatible with FreeRTOS even when        */
/*  the callback avoids FreeRTOS API.  Using 1 ms polling instead.             */
/* -------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------- */
/*  Shared RX drain helper (caller must hold s_spi_mtx)                        */
/* -------------------------------------------------------------------------- */
static void drain_rx_to_queue(void)
{
    can_frame_t mcp_frame;
    int max_reads = 4;
    while (max_reads-- > 0 && mcp2515_receive(&s_can_dev, &mcp_frame))
    {
        /* Check for CAN bootloader trigger */
        static const uint8_t bl_magic[8] = {
            0xB0, 0x07, 0x10, 0xAD, 0xB0, 0x07, 0x10, 0xAD};
        static const uint8_t bootsel_magic[8] = {
            0xB0, 0x07, 0x5E, 0x1E, 0xB0, 0x07, 0x5E, 0x1E};
        if (mcp_frame.id == BL_CAN_TRIGGER_ID &&
            mcp_frame.dlc == 8 &&
            !mcp_frame.ext)
        {
            if (memcmp(mcp_frame.data, bl_magic, 8) == 0)
            {
                xSemaphoreGive(s_spi_mtx);
                bootloader_trigger = BOOTLOADER_MAGIC;
                watchdog_reboot(0, 0, 100);
                vTaskSuspend(NULL);
            }
            if (memcmp(mcp_frame.data, bootsel_magic, 8) == 0)
            {
                xSemaphoreGive(s_spi_mtx);
                extern void pcan_trigger_bootsel(void);
                pcan_trigger_bootsel();
                vTaskSuspend(NULL);
            }
        }

        pcan_can_frame_t pf;
        pf.id = mcp_frame.id;
        pf.dlc = mcp_frame.dlc;
        pf.flags = 0;
        if (mcp_frame.ext)
            pf.flags |= PCAN_FRAME_FLAG_EFF;
        if (mcp_frame.rtr)
            pf.flags |= PCAN_FRAME_FLAG_RTR;
        memcpy(pf.data, mcp_frame.data, mcp_frame.dlc);

        xQueueSendToBack(s_rx_queue, &pf, 0);
    }
}

/* -------------------------------------------------------------------------- */
/*  CAN TX task                                                                */
/* -------------------------------------------------------------------------- */
static void can_tx_task(void *param)
{
    (void)param;
    pcan_can_frame_t frame;

    /* Bitmasks indexed by TX buffer (0-2) for READ_STATUS byte */
    static const uint8_t txreq_bit[3] = {
        MCP_STAT_TX0REQ, MCP_STAT_TX1REQ, MCP_STAT_TX2REQ};

    for (;;)
    {
        if (xQueueReceive(s_tx_queue, &frame, portMAX_DELAY) == pdTRUE)
        {
            if (!s_bus_active)
                continue;

            /* Convert pcan_can_frame_t -> can_frame_t */
            can_frame_t mcp_frame;
            mcp_frame.id = frame.id;
            mcp_frame.dlc = frame.dlc;
            mcp_frame.ext = (frame.flags & PCAN_FRAME_FLAG_EFF) ? 1 : 0;
            mcp_frame.rtr = (frame.flags & PCAN_FRAME_FLAG_RTR) ? 1 : 0;
            memcpy(mcp_frame.data, frame.data, frame.dlc);

            /* Find any free TX buffer (0-2).  All 3 MCP2515 TX buffers
             * are used to pipeline transmissions and survive arbitration
             * losses without stalling the queue. */
            for (int attempt = 0; attempt < 500; attempt++)
            {
                xSemaphoreTake(s_spi_mtx, portMAX_DELAY);
                drain_rx_to_queue();
                uint8_t stat = mcp2515_quick_status(&s_can_dev);

                int buf_idx = -1;
                for (int b = 0; b < 3; b++)
                {
                    if (!(stat & txreq_bit[b]))
                    {
                        buf_idx = b;
                        break;
                    }
                }

                if (buf_idx >= 0)
                {
                    mcp2515_load_tx_buf(&s_can_dev,
                                        (uint8_t)buf_idx, &mcp_frame);
                    xSemaphoreGive(s_spi_mtx);
                    break;
                }

                xSemaphoreGive(s_spi_mtx);
                taskYIELD();
            }
        }
    }
}

/* -------------------------------------------------------------------------- */
/*  CAN RX task                                                                */
/* -------------------------------------------------------------------------- */
static void can_rx_task(void *param)
{
    (void)param;

    for (;;)
    {
        /* INT pin is active-low: low = frame(s) waiting in RX buffer.
         * Checking GPIO is ~1 cycle vs 1 ms blind sleep, cutting
         * worst-case latency from 1 ms to ~15 us (one SPI read). */
        if (!s_bus_active || gpio_get(s_can_dev.pin_int))
        {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        xSemaphoreTake(s_spi_mtx, portMAX_DELAY);
        drain_rx_to_queue();
        xSemaphoreGive(s_spi_mtx);

        /* Yield to let same-priority tasks (TX) run, then immediately
         * re-check INT without sleeping -- catches back-to-back frames. */
        taskYIELD();
    }
}

/* -------------------------------------------------------------------------- */
/*  Public: enqueue frame for TX                                               */
/* -------------------------------------------------------------------------- */
void can_bridge_enqueue_tx(const pcan_can_frame_t *frame)
{
    if (s_tx_queue)
    {
        /* Non-blocking from USB ISR/task context */
        xQueueSendToBack(s_tx_queue, frame, 0);
    }
}

/* -------------------------------------------------------------------------- */
/*  Public: poll RX queue and send to host via USB                             */
/* -------------------------------------------------------------------------- */
extern bool pcan_usb_send_rx_msg(const uint8_t *buf, uint16_t len);
extern bool pcan_usb_is_rx_busy(void);

int can_bridge_poll_rx(void)
{
    if (pcan_usb_is_rx_busy())
        return 0;

    pcan_can_frame_t frames[4];
    int count = 0;

    while (count < 4)
    {
        if (xQueueReceive(s_rx_queue, &frames[count], 0) != pdTRUE)
            break;
        count++;
    }

    if (count == 0)
        return 0;

    /* Get timestamp: microseconds -> PCAN ticks */
    uint64_t us = time_us_64();
    uint16_t ts = (uint16_t)((us * PCAN_TS_US_TO_TICK_NUM) / PCAN_TS_US_TO_TICK_DEN);

    uint8_t buf[PCAN_USB_EP_SIZE];
    int len = pcan_encode_rx_msg(buf, frames, count, ts);
    if (len > 0)
    {
        pcan_usb_send_rx_msg(buf, (uint16_t)len);
    }

    return count;
}

/* -------------------------------------------------------------------------- */
/*  Public: reconfigure MCP2515 with new bitrate                               */
/* -------------------------------------------------------------------------- */
void can_bridge_reconfigure(void)
{
    uint8_t cnf1, cnf2, cnf3;
    pcan_get_mcp_timing(&cnf1, &cnf2, &cnf3);

    /* Mark bus inactive FIRST so TX/RX tasks stop touching SPI.
     * This prevents them from spin-waiting on the mutex while
     * we hold it for the 20+ ms reconfigure window. */
    s_bus_active = false;

    xSemaphoreTake(s_spi_mtx, portMAX_DELAY);

    /* Flush stale frames from previous bus session */
    xQueueReset(s_tx_queue);
    xQueueReset(s_rx_queue);

    /* Always do a full reconfigure (software reset + configure).
     * The MCP2515 accumulates error state across bus cycles and can get
     * stuck in bus-off or error-passive after repeated open/close.
     * A full reset clears error counters and all internal state.
     * The ~20 ms CONFIG-mode window is acceptable because the host
     * driver exchanges several USB commands between bus-on and the
     * first CAN frame, providing enough margin. */
    bool ok = false;
    for (int attempt = 0; attempt < 3 && !ok; attempt++)
    {
        if (attempt > 0)
        {
            /* Hardware reset on retry */
            gpio_put(s_can_dev.pin_rst, 0);
            sleep_ms(10);
            gpio_put(s_can_dev.pin_rst, 1);
            sleep_ms(10);
        }
        ok = mcp2515_reconfigure(&s_can_dev, cnf1, cnf2, cnf3);
    }

    xSemaphoreGive(s_spi_mtx);

    s_bus_active = ok;
}

/* -------------------------------------------------------------------------- */
/*  Public: create tasks                                                       */
/* -------------------------------------------------------------------------- */
void can_bridge_create_tasks(void)
{
    /* Initialize MCP2515 hardware */
    s_can_dev.spi = CAN_SPI_INSTANCE;
    s_can_dev.pin_cs = PIN_CAN_CS;
    s_can_dev.pin_rst = PIN_CAN_RST;
    s_can_dev.pin_int = PIN_CAN_INT;
    s_can_dev.pin_miso = PIN_CAN_MISO;
    s_can_dev.pin_mosi = PIN_CAN_MOSI;
    s_can_dev.pin_sck = PIN_CAN_SCK;

    mcp2515_hw_init(&s_can_dev);

    /* Create synchronization primitives */
    s_tx_queue = xQueueCreate(TX_QUEUE_DEPTH, sizeof(pcan_can_frame_t));
    s_rx_queue = xQueueCreate(RX_QUEUE_DEPTH, sizeof(pcan_can_frame_t));
    s_spi_mtx = xSemaphoreCreateMutex();

    /* GPIO ISR disabled -- see comment at top of file.
     * RX task uses 1 ms polling to read MCP2515 status registers. */

    /* Create tasks */
    xTaskCreate(can_tx_task, "can_tx", CAN_TX_STACK, NULL,
                CAN_TX_TASK_PRIORITY, NULL);
    xTaskCreate(can_rx_task, "can_rx", CAN_RX_STACK, NULL,
                CAN_RX_TASK_PRIORITY, NULL);

    s_bus_active = false;
}

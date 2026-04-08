/*
 * bootloader.c - CAN firmware update bootloader for BladeCore-M54C
 *
 * On reset, main() calls bootloader_check(). If the persistent magic word
 * is set (from a USB vendor request or a CAN trigger frame), the device
 * enters a bare-metal CAN bootloader that can receive new firmware over
 * the MCP2515 CAN bus, then reboot into the application.
 *
 * If the magic word is not set, bootloader_check() returns immediately
 * and normal FreeRTOS application startup continues.
 */

#include "bootloader/bootloader.h"
#include "CONFIG.h"
#include "drivers/mcp2515.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/resets.h"
#include <string.h>

/* -------------------------------------------------------------------------- */
/*  Persistent trigger word (survives watchdog reset)                          */
/* -------------------------------------------------------------------------- */
__attribute__((section(".uninitialized_data"))) volatile uint32_t bootloader_trigger;

/* -------------------------------------------------------------------------- */
/*  Flash layout                                                               */
/* -------------------------------------------------------------------------- */
/* Application starts at 64 KB offset (first 64 KB reserved for bootloader).
 * RP2354B has 4 MB flash; application region: 64K .. 4M.                     */
#define APP_FLASH_OFFSET (64u * 1024u)
#define APP_MAX_SIZE (4u * 1024u * 1024u - APP_FLASH_OFFSET)
#define FLASH_PAGE_SIZE_BL 256u
#define FLASH_SECTOR_SIZE_BL 4096u

/* -------------------------------------------------------------------------- */
/*  Simple CRC-32 (no table, compact)                                          */
/* -------------------------------------------------------------------------- */
static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    crc = ~crc;
    for (uint32_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (-(crc & 1u)));
        }
    }
    return ~crc;
}

/* -------------------------------------------------------------------------- */
/*  Bootloader schedule reboot (called from USB context)                       */
/* -------------------------------------------------------------------------- */
void bootloader_schedule_reboot(void)
{
    /* Watchdog reboot after 100ms to allow USB status stage */
    watchdog_reboot(0, 0, 100);
}

/* -------------------------------------------------------------------------- */
/*  Bootloader main loop (bare-metal, no FreeRTOS)                             */
/* -------------------------------------------------------------------------- */
static void bootloader_run(void)
{
    /* Clear the trigger so we don't loop on next boot */
    bootloader_trigger = 0;

    /* Init heartbeat LED to indicate bootloader mode (rapid blink) */
    gpio_init(PIN_HEARTBEAT);
    gpio_set_dir(PIN_HEARTBEAT, GPIO_OUT);

    /* Init MCP2515 at 1 Mbps (default) */
    mcp2515_t can_dev;
    can_dev.spi = CAN_SPI_INSTANCE;
    can_dev.pin_cs = PIN_CAN_CS;
    can_dev.pin_rst = PIN_CAN_RST;
    can_dev.pin_int = PIN_CAN_INT;
    can_dev.pin_miso = PIN_CAN_MISO;
    can_dev.pin_mosi = PIN_CAN_MOSI;
    can_dev.pin_sck = PIN_CAN_SCK;

    if (!mcp2515_init(&can_dev))
    {
        /* CAN init failed -- fast blink and halt */
        for (;;)
        {
            gpio_put(PIN_HEARTBEAT, 1);
            sleep_ms(50);
            gpio_put(PIN_HEARTBEAT, 0);
            sleep_ms(50);
        }
    }

    /* Send bootloader ready announcement */
    can_frame_t rsp;
    rsp.id = BL_RSP_ID;
    rsp.dlc = 2;
    rsp.ext = 0;
    rsp.rtr = 0;
    memset(rsp.data, 0, 8);
    rsp.data[0] = BL_RSP_ACK;
    rsp.data[1] = BL_CMD_STATUS;
    mcp2515_send(&can_dev, &rsp);

    /* Bootloader state */
    uint32_t fw_size = 0;
    uint32_t fw_received = 0;
    uint32_t crc = 0;
    uint8_t page_buf[FLASH_PAGE_SIZE_BL];
    uint32_t page_offset = 0;
    uint32_t flash_write_offset = APP_FLASH_OFFSET;
    bool erased = false;
    uint32_t blink_timer = 0;
    bool led = false;

    for (;;)
    {
        /* Rapid blink in bootloader mode */
        if (++blink_timer > 10000)
        {
            blink_timer = 0;
            led = !led;
            gpio_put(PIN_HEARTBEAT, led);
        }

        can_frame_t rx;
        if (!mcp2515_receive(&can_dev, &rx))
        {
            continue;
        }

        /* Only process standard frames on BL_CMD_ID */
        if (rx.id != BL_CMD_ID || rx.dlc < 1)
        {
            continue;
        }

        uint8_t cmd = rx.data[0];
        memset(rsp.data, 0, 8);
        rsp.id = BL_RSP_ID;
        rsp.ext = 0;
        rsp.rtr = 0;

        switch (cmd)
        {

        case BL_CMD_STATUS:
            rsp.dlc = 3;
            rsp.data[0] = BL_RSP_ACK;
            rsp.data[1] = BL_CMD_STATUS;
            rsp.data[2] = erased ? 0x01 : 0x00;
            mcp2515_send(&can_dev, &rsp);
            break;

        case BL_CMD_ERASE:
            /* Erase application flash region */
            if (rx.dlc >= 5)
            {
                fw_size = (uint32_t)rx.data[1] | ((uint32_t)rx.data[2] << 8) | ((uint32_t)rx.data[3] << 16) | ((uint32_t)rx.data[4] << 24);
            }
            else
            {
                fw_size = APP_MAX_SIZE;
            }
            if (fw_size > APP_MAX_SIZE)
                fw_size = APP_MAX_SIZE;

            /* Round up to sector boundary */
            uint32_t erase_len = (fw_size + FLASH_SECTOR_SIZE_BL - 1) & ~(FLASH_SECTOR_SIZE_BL - 1);

            uint32_t saved_interrupts = save_and_disable_interrupts();
            flash_range_erase(APP_FLASH_OFFSET, erase_len);
            restore_interrupts(saved_interrupts);

            fw_received = 0;
            page_offset = 0;
            flash_write_offset = APP_FLASH_OFFSET;
            crc = 0;
            erased = true;

            rsp.dlc = 2;
            rsp.data[0] = BL_RSP_ACK;
            rsp.data[1] = BL_CMD_ERASE;
            mcp2515_send(&can_dev, &rsp);
            break;

        case BL_CMD_START:
            if (rx.dlc >= 5)
            {
                fw_size = (uint32_t)rx.data[1] | ((uint32_t)rx.data[2] << 8) | ((uint32_t)rx.data[3] << 16) | ((uint32_t)rx.data[4] << 24);
            }
            fw_received = 0;
            page_offset = 0;
            flash_write_offset = APP_FLASH_OFFSET;
            crc = 0;

            rsp.dlc = 2;
            rsp.data[0] = BL_RSP_ACK;
            rsp.data[1] = BL_CMD_START;
            mcp2515_send(&can_dev, &rsp);
            break;

        case BL_CMD_DATA:
        {
            if (!erased)
            {
                rsp.dlc = 2;
                rsp.data[0] = BL_RSP_NAK;
                rsp.data[1] = BL_CMD_DATA;
                mcp2515_send(&can_dev, &rsp);
                break;
            }

            /* data[1..7] = up to 7 bytes of firmware data */
            uint8_t chunk_len = rx.dlc - 1;
            if (chunk_len > 7)
                chunk_len = 7;

            for (uint8_t i = 0; i < chunk_len; i++)
            {
                page_buf[page_offset++] = rx.data[1 + i];
                fw_received++;

                if (page_offset >= FLASH_PAGE_SIZE_BL || fw_received >= fw_size)
                {
                    /* Pad remainder with 0xFF */
                    while (page_offset < FLASH_PAGE_SIZE_BL)
                    {
                        page_buf[page_offset++] = 0xFF;
                    }

                    /* Update CRC before padding takes effect (only count real data) */
                    uint32_t real_len = (fw_received >= fw_size)
                                            ? (fw_size - (fw_received - chunk_len + i + 1) + 1)
                                            : FLASH_PAGE_SIZE_BL;
                    /* Simpler: just CRC the raw received bytes */

                    uint32_t saved = save_and_disable_interrupts();
                    flash_range_program(flash_write_offset, page_buf, FLASH_PAGE_SIZE_BL);
                    restore_interrupts(saved);

                    flash_write_offset += FLASH_PAGE_SIZE_BL;
                    page_offset = 0;
                }
            }

            /* CRC the incoming chunk */
            crc = crc32_update(crc, &rx.data[1], chunk_len);

            /* ACK every data frame (flow control) */
            rsp.dlc = 6;
            rsp.data[0] = BL_RSP_ACK;
            rsp.data[1] = BL_CMD_DATA;
            rsp.data[2] = (uint8_t)(fw_received);
            rsp.data[3] = (uint8_t)(fw_received >> 8);
            rsp.data[4] = (uint8_t)(fw_received >> 16);
            rsp.data[5] = (uint8_t)(fw_received >> 24);
            mcp2515_send(&can_dev, &rsp);
            break;
        }

        case BL_CMD_VERIFY:
        {
            uint32_t expected_crc = 0;
            if (rx.dlc >= 5)
            {
                expected_crc = (uint32_t)rx.data[1] | ((uint32_t)rx.data[2] << 8) | ((uint32_t)rx.data[3] << 16) | ((uint32_t)rx.data[4] << 24);
            }

            rsp.dlc = 6;
            if (crc == expected_crc)
            {
                rsp.data[0] = BL_RSP_ACK;
            }
            else
            {
                rsp.data[0] = BL_RSP_NAK;
            }
            rsp.data[1] = BL_CMD_VERIFY;
            rsp.data[2] = (uint8_t)(crc);
            rsp.data[3] = (uint8_t)(crc >> 8);
            rsp.data[4] = (uint8_t)(crc >> 16);
            rsp.data[5] = (uint8_t)(crc >> 24);
            mcp2515_send(&can_dev, &rsp);
            break;
        }

        case BL_CMD_REBOOT:
            rsp.dlc = 2;
            rsp.data[0] = BL_RSP_ACK;
            rsp.data[1] = BL_CMD_REBOOT;
            mcp2515_send(&can_dev, &rsp);
            sleep_ms(50);
            watchdog_reboot(0, 0, 0);
            for (;;)
            {
                tight_loop_contents();
            }
            break;

        default:
            rsp.dlc = 2;
            rsp.data[0] = BL_RSP_NAK;
            rsp.data[1] = cmd;
            mcp2515_send(&can_dev, &rsp);
            break;
        }
    }
}

/* -------------------------------------------------------------------------- */
/*  bootloader_check - called from main() before FreeRTOS                      */
/* -------------------------------------------------------------------------- */
void bootloader_check(void)
{
    if (bootloader_trigger == BOOTLOADER_MAGIC)
    {
        bootloader_run();
        /* Never returns */
    }
}

/*
 * Heartbeat.c - BladeCore-M54C CAN test firmware
 *
 * Initialises the MCP2515 CAN controller over SPI1, then:
 *   - Sends a periodic heartbeat frame (ID 0x100) with counter every second
 *   - Echoes any received CAN frame back with ID = received_ID + 1
 *   - Handles command frames on ID 0x7E0, replies on 0x7E1:
 *       0x01 PING          -> echo payload back
 *       0x02 GET_ERRORS    -> [OK, TEC, REC, EFLG]
 *       0x03 LOOPBACK_ON   -> enter MCP2515 loopback mode
 *       0x04 LOOPBACK_OFF  -> return to normal mode
 *       0x05 GET_STATUS    -> [OK, CANSTAT]
 *       0x06 ECHO_PAYLOAD  -> echo data[1..7] back unchanged
 *   - Blinks heartbeat LED on each TX
 *   - Prints status over USB serial
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "CONFIG.h"
#include "mcp2515.h"

static mcp2515_t can_dev;

static void handle_command(const can_frame_t *rx)
{
    can_frame_t rsp;
    rsp.id = CMD_RESPONSE_ID;
    memset(rsp.data, 0, sizeof(rsp.data));

    uint8_t cmd = rx->data[0];

    switch (cmd)
    {
    case CMD_PING:
        /* Echo the entire frame payload back */
        rsp.dlc = rx->dlc;
        memcpy(rsp.data, rx->data, rx->dlc);
        rsp.data[0] = RSP_OK;
        break;

    case CMD_GET_ERRORS:
    {
        mcp2515_errors_t err;
        mcp2515_read_errors(&can_dev, &err);
        rsp.dlc = 4;
        rsp.data[0] = RSP_OK;
        rsp.data[1] = err.tec;
        rsp.data[2] = err.rec;
        rsp.data[3] = err.eflg;
        printf("CMD GET_ERRORS -> TEC=%u REC=%u EFLG=0x%02X\r\n",
               err.tec, err.rec, err.eflg);
        break;
    }

    case CMD_LOOPBACK_ON:
        rsp.dlc = 2;
        rsp.data[0] = mcp2515_set_loopback(&can_dev) ? RSP_OK : RSP_FAIL;
        rsp.data[1] = mcp2515_status(&can_dev);
        printf("CMD LOOPBACK_ON -> 0x%02X\r\n", rsp.data[1]);
        break;

    case CMD_LOOPBACK_OFF:
        rsp.dlc = 2;
        rsp.data[0] = mcp2515_set_normal(&can_dev) ? RSP_OK : RSP_FAIL;
        rsp.data[1] = mcp2515_status(&can_dev);
        printf("CMD LOOPBACK_OFF -> 0x%02X\r\n", rsp.data[1]);
        break;

    case CMD_GET_STATUS:
        rsp.dlc = 2;
        rsp.data[0] = RSP_OK;
        rsp.data[1] = mcp2515_status(&can_dev);
        printf("CMD GET_STATUS -> 0x%02X\r\n", rsp.data[1]);
        break;

    case CMD_ECHO_PAYLOAD:
        /* Echo data[1..7] back in data[1..7], data[0]=OK */
        rsp.dlc = rx->dlc;
        memcpy(rsp.data, rx->data, rx->dlc);
        rsp.data[0] = RSP_OK;
        break;

    default:
        rsp.dlc = 2;
        rsp.data[0] = RSP_FAIL;
        rsp.data[1] = cmd;
        printf("CMD UNKNOWN 0x%02X\r\n", cmd);
        break;
    }

    mcp2515_send(&can_dev, &rsp);
}

int main(void)
{
    stdio_init_all();

    /* Heartbeat LED as simple on/off indicator */
    gpio_init(PIN_HEARTBEAT);
    gpio_set_dir(PIN_HEARTBEAT, GPIO_OUT);
    gpio_put(PIN_HEARTBEAT, 0);

    /* Configure MCP2515 */
    can_dev.spi = CAN_SPI_INSTANCE;
    can_dev.pin_cs = PIN_CAN_CS;
    can_dev.pin_rst = PIN_CAN_RST;
    can_dev.pin_int = PIN_CAN_INT;
    can_dev.pin_miso = PIN_CAN_MISO;
    can_dev.pin_mosi = PIN_CAN_MOSI;
    can_dev.pin_sck = PIN_CAN_SCK;

    printf("\r\n--- BladeCore-M54C CAN Test v2 ---\r\n");
    printf("MCP2515 init... ");

    if (!mcp2515_init(&can_dev))
    {
        uint8_t stat = mcp2515_status(&can_dev);
        printf("FAILED (CANSTAT=0x%02X)\r\n", stat);
        while (true)
        {
            gpio_put(PIN_HEARTBEAT, 1);
            sleep_ms(100);
            gpio_put(PIN_HEARTBEAT, 0);
            sleep_ms(100);
        }
    }
    printf("OK (normal mode)\r\n");
    printf("CAN bus: 1 Mbps, 16 MHz osc\r\n");
    printf("CMD ID=0x7E0, RSP ID=0x7E1, echo on all other IDs\r\n\r\n");

    uint32_t tx_count = 0;
    bool led_state = false;

    while (true)
    {
        /* --- TX: send heartbeat counter frame --- */
        can_frame_t tx_frame;
        tx_frame.id = 0x100;
        tx_frame.dlc = 4;
        tx_frame.data[0] = (uint8_t)(tx_count >> 24);
        tx_frame.data[1] = (uint8_t)(tx_count >> 16);
        tx_frame.data[2] = (uint8_t)(tx_count >> 8);
        tx_frame.data[3] = (uint8_t)(tx_count);

        bool ok = mcp2515_send(&can_dev, &tx_frame);
        printf("TX #%lu [%02X %02X %02X %02X] %s\r\n",
               (unsigned long)tx_count,
               tx_frame.data[0], tx_frame.data[1],
               tx_frame.data[2], tx_frame.data[3],
               ok ? "OK" : "FAIL");
        tx_count++;

        led_state = !led_state;
        gpio_put(PIN_HEARTBEAT, led_state);

        /* --- RX: process received frames for ~1 second --- */
        for (int i = 0; i < 100; i++)
        {
            can_frame_t rx_frame;
            if (mcp2515_receive(&can_dev, &rx_frame))
            {
                printf("RX ID=0x%03lX DLC=%u [",
                       (unsigned long)rx_frame.id, rx_frame.dlc);
                for (int d = 0; d < rx_frame.dlc; d++)
                {
                    printf("%02X%s", rx_frame.data[d],
                           d < rx_frame.dlc - 1 ? " " : "");
                }
                printf("]\r\n");

                if (rx_frame.id == CMD_REQUEST_ID)
                {
                    handle_command(&rx_frame);
                }
                else
                {
                    /* Echo back with ID+1 */
                    can_frame_t echo = rx_frame;
                    echo.id = rx_frame.id + 1;
                    mcp2515_send(&can_dev, &echo);
                    printf("ECHO ID=0x%03lX\r\n", (unsigned long)echo.id);
                }
            }
            sleep_ms(10);
        }
    }
}

/*
 * mcp2515.h - Minimal MCP2515 SPI CAN controller driver for RP2350B
 *
 * Supports: reset, init, configure bit timing, send standard CAN frames,
 *           receive standard CAN frames.
 * Crystal: 16 MHz assumed.
 */

#ifndef MCP2515_H
#define MCP2515_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/spi.h"

/* ---- MCP2515 SPI Instructions ------------------------------------------ */
#define MCP_RESET 0xC0
#define MCP_READ 0x03
#define MCP_WRITE 0x02
#define MCP_BITMOD 0x05
#define MCP_READ_STATUS 0xA0
#define MCP_READ_RX0 0x90 /* Read RX buffer 0 starting at RXB0SIDH */
#define MCP_LOAD_TX0 0x40 /* Load TX buffer 0 starting at TXB0SIDH */
#define MCP_RTS_TX0 0x81  /* Request to send TX buffer 0 */

/* ---- MCP2515 Registers ------------------------------------------------- */
#define REG_CANSTAT 0x0E
#define REG_CANCTRL 0x0F
#define REG_CNF3 0x28
#define REG_CNF2 0x29
#define REG_CNF1 0x2A
#define REG_CANINTF 0x2C
#define REG_CANINTE 0x2B
#define REG_TXB0CTRL 0x30
#define REG_EFLG 0x2D
#define REG_TEC 0x1C
#define REG_REC 0x1D
#define REG_RXB0CTRL 0x60
#define REG_RXB1CTRL 0x70

/* ---- CANCTRL mode bits ------------------------------------------------- */
#define MODE_NORMAL 0x00
#define MODE_SLEEP 0x20
#define MODE_LOOPBACK 0x40
#define MODE_LISTENONLY 0x60
#define MODE_CONFIG 0x80
#define MODE_MASK 0xE0

/* ---- Interrupt flags --------------------------------------------------- */
#define CANINTF_RX0IF 0x01
#define CANINTF_RX1IF 0x02
#define CANINTF_TX0IF 0x04
#define CANINTF_MERRF 0x80
#define CANINTF_ERRIF 0x20

/* ---- CNF bit-timing for 1 Mbps @ 16 MHz osc ----------------------------- */
/*  TQ = 2/16MHz = 125ns, 8 TQ per bit:                                     */
/*  SyncSeg=1TQ, PropSeg=1TQ, PS1=3TQ, PS2=3TQ  -> 8TQ total              */
/*  SJW = 1TQ                                                                */
#define CNF1_1MBPS 0x00 /* SJW=1TQ, BRP=0 (TQ=2/Fosc) */
#define CNF2_1MBPS 0x90 /* BTLMODE=1, SAM=0, PHSEG1=2(3TQ), PRSEG=0(1TQ) */
#define CNF3_1MBPS 0x02 /* PHSEG2=2 (3TQ) */

/* ---- Driver context ---------------------------------------------------- */
typedef struct
{
    spi_inst_t *spi;
    uint pin_cs;
    uint pin_rst;
    uint pin_int;
    uint pin_miso;
    uint pin_mosi;
    uint pin_sck;
} mcp2515_t;

/* ---- CAN frame --------------------------------------------------------- */
typedef struct
{
    uint32_t id; /* 11-bit standard ID */
    uint8_t dlc; /* data length 0..8 */
    uint8_t data[8];
} can_frame_t;

/* ---- API --------------------------------------------------------------- */

/* Initialise SPI, reset MCP2515, configure 1 Mbps, enter normal mode.
 * Returns true on success. */
bool mcp2515_init(mcp2515_t *dev);

/* Send a standard CAN frame via TX buffer 0.
 * Returns true if transmitted within ~10 ms. */
bool mcp2515_send(mcp2515_t *dev, const can_frame_t *frame);

/* Poll for a received frame in RX buffer 0.
 * Returns true if a frame was available. */
bool mcp2515_receive(mcp2515_t *dev, can_frame_t *frame);

/* Read a single register. */
uint8_t mcp2515_read_reg(mcp2515_t *dev, uint8_t addr);

/* Read CANSTAT and return the mode/status byte. */
uint8_t mcp2515_status(mcp2515_t *dev);

/* Read TX/RX error counters and error flag register. */
typedef struct
{
    uint8_t tec;  /* TX error counter */
    uint8_t rec;  /* RX error counter */
    uint8_t eflg; /* Error flag register (EFLG) */
} mcp2515_errors_t;

void mcp2515_read_errors(mcp2515_t *dev, mcp2515_errors_t *err);

/* Switch MCP2515 to loopback mode. Returns true on success. */
bool mcp2515_set_loopback(mcp2515_t *dev);

/* Switch MCP2515 back to normal mode. Returns true on success. */
bool mcp2515_set_normal(mcp2515_t *dev);

/* -------------------------------------------------------------------------- */
/*  Command protocol: PC sends frames to ID 0x7E0, board replies on 0x7E1     */
/* -------------------------------------------------------------------------- */
#define CMD_REQUEST_ID 0x7E0
#define CMD_RESPONSE_ID 0x7E1

/* Command bytes (data[0]) */
#define CMD_PING 0x01         /* Ping -> Pong (data echoed back)          */
#define CMD_GET_ERRORS 0x02   /* Read error counters -> [TEC, REC, EFLG]  */
#define CMD_LOOPBACK_ON 0x03  /* Enter loopback mode                      */
#define CMD_LOOPBACK_OFF 0x04 /* Return to normal mode                    */
#define CMD_GET_STATUS 0x05   /* Read CANSTAT -> [CANSTAT]                 */
#define CMD_ECHO_PAYLOAD 0x06 /* Echo data[1..7] back unchanged            */

/* Response status */
#define RSP_OK 0x00
#define RSP_FAIL 0xFF

#endif /* MCP2515_H */

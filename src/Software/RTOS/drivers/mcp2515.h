/*
 * mcp2515.h - MCP2515 SPI CAN controller driver for RP2350B
 *
 * Supports: reset, init, configurable bit timing, send/receive
 *           standard and extended CAN frames, RTR.
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
#define MCP_READ_RX1 0x94 /* Read RX buffer 1 starting at RXB1SIDH */
#define MCP_LOAD_TX0 0x40 /* Load TX buffer 0 starting at TXB0SIDH */
#define MCP_LOAD_TX1 0x42 /* Load TX buffer 1 starting at TXB1SIDH */
#define MCP_LOAD_TX2 0x44 /* Load TX buffer 2 starting at TXB2SIDH */
#define MCP_RTS_TX0 0x81  /* Request to send TX buffer 0 */
#define MCP_RTS_TX1 0x82  /* Request to send TX buffer 1 */
#define MCP_RTS_TX2 0x84  /* Request to send TX buffer 2 */

/* ---- MCP2515 Registers ------------------------------------------------- */
#define REG_CANSTAT 0x0E
#define REG_CANCTRL 0x0F
#define REG_CNF3 0x28
#define REG_CNF2 0x29
#define REG_CNF1 0x2A
#define REG_CANINTF 0x2C
#define REG_CANINTE 0x2B
#define REG_TXB0CTRL 0x30
#define REG_TXB1CTRL 0x40
#define REG_TXB2CTRL 0x50
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
#define CANINTF_TX1IF 0x08
#define CANINTF_TX2IF 0x10
#define CANINTF_MERRF 0x80
#define CANINTF_ERRIF 0x20

/* ---- READ STATUS (0xA0) response bit masks ----------------------------- */
#define MCP_STAT_RX0IF 0x01
#define MCP_STAT_RX1IF 0x02
#define MCP_STAT_TX0REQ 0x04
#define MCP_STAT_TX0IF 0x08
#define MCP_STAT_TX1REQ 0x10
#define MCP_STAT_TX1IF 0x20
#define MCP_STAT_TX2REQ 0x40
#define MCP_STAT_TX2IF 0x80

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
    uint32_t id; /* 11-bit standard or 29-bit extended ID */
    uint8_t dlc; /* data length 0..8 */
    uint8_t ext; /* 1 = extended (29-bit), 0 = standard (11-bit) */
    uint8_t rtr; /* 1 = remote transmission request */
    uint8_t data[8];
} can_frame_t;

/* ---- API --------------------------------------------------------------- */

/* Initialize SPI and GPIO pins only (no MCP2515 config). */
void mcp2515_hw_init(mcp2515_t *dev);

/* Full init: reset MCP2515, configure 1 Mbps, enter normal mode.
 * Returns true on success. */
bool mcp2515_init(mcp2515_t *dev);

/* Reconfigure bit timing with specific CNF values.
 * Performs software reset, applies timing, enters normal mode. */
bool mcp2515_reconfigure(mcp2515_t *dev, uint8_t cnf1, uint8_t cnf2, uint8_t cnf3);

/* Send a CAN frame via TX buffer 0.
 * Returns true if transmitted within ~10 ms. */
bool mcp2515_send(mcp2515_t *dev, const can_frame_t *frame);

/* Load a CAN frame into TX buffer 0 and issue Request-To-Send.
 * Returns immediately without waiting for transmission. */
void mcp2515_send_start(mcp2515_t *dev, const can_frame_t *frame);

/* Check if TX buffer 0 has finished transmitting.
 * Returns true and clears the TX0IF flag if complete. */
bool mcp2515_tx_complete(mcp2515_t *dev);

/* One-shot READ STATUS instruction (2 bytes SPI). Returns combined
 * TX-request, TX-complete, and RX-ready bits. */
uint8_t mcp2515_quick_status(mcp2515_t *dev);

/* Load a CAN frame into TX buffer buf_idx (0-2) and issue RTS.
 * Returns immediately without waiting for transmission. */
void mcp2515_load_tx_buf(mcp2515_t *dev, uint8_t buf_idx,
                         const can_frame_t *frame);

/* Poll for a received frame in RX buffer 0 or 1.
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

/* ---- Default 1 Mbps bit timing @ 16 MHz osc ----------------------------- */
#define CNF1_1MBPS 0x00
#define CNF2_1MBPS 0x90
#define CNF3_1MBPS 0x02

#endif /* MCP2515_H */

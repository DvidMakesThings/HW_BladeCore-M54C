/*
 * mcp2515.c - MCP2515 SPI CAN controller driver for RP2350B
 *
 * Supports standard (11-bit) and extended (29-bit) CAN frames, RTR,
 * configurable bit timing, and both RX buffers.
 */

#include "drivers/mcp2515.h"
#include "CONFIG.h"
#include "pico/stdlib.h"
#include <string.h>

/* ---- Low-level SPI helpers --------------------------------------------- */

static inline void cs_select(mcp2515_t *dev)
{
    gpio_put(dev->pin_cs, 0);
}

static inline void cs_deselect(mcp2515_t *dev)
{
    gpio_put(dev->pin_cs, 1);
}

static void mcp_write_reg(mcp2515_t *dev, uint8_t addr, uint8_t val)
{
    uint8_t buf[3] = {MCP_WRITE, addr, val};
    cs_select(dev);
    spi_write_blocking(dev->spi, buf, 3);
    cs_deselect(dev);
}

uint8_t mcp2515_read_reg(mcp2515_t *dev, uint8_t addr)
{
    uint8_t buf[3] = {MCP_READ, addr, 0x00};
    uint8_t rx[3];
    cs_select(dev);
    spi_write_read_blocking(dev->spi, buf, rx, 3);
    cs_deselect(dev);
    return rx[2];
}

static void mcp_bit_modify(mcp2515_t *dev, uint8_t addr,
                           uint8_t mask, uint8_t val)
{
    uint8_t buf[4] = {MCP_BITMOD, addr, mask, val};
    cs_select(dev);
    spi_write_blocking(dev->spi, buf, 4);
    cs_deselect(dev);
}

static void mcp_reset(mcp2515_t *dev)
{
    uint8_t cmd = MCP_RESET;
    cs_select(dev);
    spi_write_blocking(dev->spi, &cmd, 1);
    cs_deselect(dev);
}

/* Quick combined TX/RX status in a 2-byte SPI transfer (vs 3-byte READ). */
uint8_t mcp2515_quick_status(mcp2515_t *dev)
{
    uint8_t tx[2] = {MCP_READ_STATUS, 0x00};
    uint8_t rx[2];
    cs_select(dev);
    spi_write_read_blocking(dev->spi, tx, rx, 2);
    cs_deselect(dev);
    return rx[1];
}

/* ---- Internal: configure after reset ----------------------------------- */
static bool mcp_configure(mcp2515_t *dev, uint8_t cnf1, uint8_t cnf2, uint8_t cnf3)
{
    /* Verify we are in configuration mode */
    uint8_t stat = mcp2515_read_reg(dev, REG_CANSTAT);
    if ((stat & MODE_MASK) != MODE_CONFIG)
    {
        return false;
    }

    /* Bit timing */
    mcp_write_reg(dev, REG_CNF1, cnf1);
    mcp_write_reg(dev, REG_CNF2, cnf2);
    mcp_write_reg(dev, REG_CNF3, cnf3);

    /* Enable RX0 and RX1 interrupts, accept all messages (no filters) */
    mcp_write_reg(dev, REG_CANINTE, CANINTF_RX0IF | CANINTF_RX1IF);
    mcp_write_reg(dev, REG_RXB0CTRL, 0x64); /* RXM=11 (no filter), BUKT=1 (rollover to RXB1) */
    mcp_write_reg(dev, REG_RXB1CTRL, 0x60); /* RXM=11 (no filter) */

    /* Clear all interrupt flags */
    mcp_write_reg(dev, REG_CANINTF, 0x00);

    /* Switch to normal mode */
    mcp_bit_modify(dev, REG_CANCTRL, MODE_MASK, MODE_NORMAL);
    sleep_ms(10);

    stat = mcp2515_read_reg(dev, REG_CANSTAT);
    return (stat & MODE_MASK) == MODE_NORMAL;
}

/* ---- Public API -------------------------------------------------------- */

void mcp2515_hw_init(mcp2515_t *dev)
{
    /* GPIO setup */
    gpio_init(dev->pin_cs);
    gpio_set_dir(dev->pin_cs, GPIO_OUT);
    gpio_put(dev->pin_cs, 1); /* deselect */

    gpio_init(dev->pin_rst);
    gpio_set_dir(dev->pin_rst, GPIO_OUT);

    gpio_init(dev->pin_int);
    gpio_set_dir(dev->pin_int, GPIO_IN);
    gpio_pull_up(dev->pin_int);

    /* Hardware reset */
    gpio_put(dev->pin_rst, 0);
    sleep_ms(10);
    gpio_put(dev->pin_rst, 1);
    sleep_ms(10);

    /* SPI setup */
    spi_init(dev->spi, CAN_SPI_BAUDRATE);
    spi_set_format(dev->spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(dev->pin_miso, GPIO_FUNC_SPI);
    gpio_set_function(dev->pin_mosi, GPIO_FUNC_SPI);
    gpio_set_function(dev->pin_sck, GPIO_FUNC_SPI);

    /* Software reset */
    mcp_reset(dev);
    sleep_ms(10);
}

bool mcp2515_init(mcp2515_t *dev)
{
    mcp2515_hw_init(dev);
    return mcp_configure(dev, CNF1_1MBPS, CNF2_1MBPS, CNF3_1MBPS);
}

bool mcp2515_reconfigure(mcp2515_t *dev, uint8_t cnf1, uint8_t cnf2, uint8_t cnf3)
{
    /* Software reset puts MCP2515 into config mode */
    mcp_reset(dev);
    sleep_ms(10);
    return mcp_configure(dev, cnf1, cnf2, cnf3);
}

bool mcp2515_send(mcp2515_t *dev, const can_frame_t *frame)
{
    uint8_t buf[14];
    buf[0] = MCP_LOAD_TX0;

    if (frame->ext)
    {
        /* Extended ID (29-bit): SIDH, SIDL (EXIDE=1), EID8, EID0 */
        uint32_t eid = frame->id;
        buf[1] = (uint8_t)(eid >> 21);                   /* SIDH: ID[28:21] */
        buf[2] = (uint8_t)(((eid >> 13) & 0xE0) | 0x08 | /* SIDL: ID[20:18] | EXIDE */
                           ((eid >> 16) & 0x03));        /*        ID[17:16] */
        buf[3] = (uint8_t)(eid >> 8);                    /* EID8: ID[15:8] */
        buf[4] = (uint8_t)(eid);                         /* EID0: ID[7:0] */
    }
    else
    {
        /* Standard ID (11-bit) */
        buf[1] = (uint8_t)(frame->id >> 3);          /* SIDH */
        buf[2] = (uint8_t)((frame->id & 0x07) << 5); /* SIDL (no ext) */
        buf[3] = 0x00;                               /* EID8 */
        buf[4] = 0x00;                               /* EID0 */
    }

    uint8_t dlc = frame->dlc & 0x0F;
    if (frame->rtr)
        dlc |= 0x40; /* RTR bit in DLC register */
    buf[5] = dlc;

    if (!frame->rtr)
    {
        memcpy(&buf[6], frame->data, frame->dlc & 0x0F);
    }

    cs_select(dev);
    spi_write_blocking(dev->spi, buf, 6 + (frame->rtr ? 0 : (frame->dlc & 0x0F)));
    cs_deselect(dev);

    /* Request to send */
    uint8_t rts = MCP_RTS_TX0;
    cs_select(dev);
    spi_write_blocking(dev->spi, &rts, 1);
    cs_deselect(dev);

    /* Wait for TX complete or timeout */
    for (int i = 0; i < 100; i++)
    {
        uint8_t intf = mcp2515_read_reg(dev, REG_CANINTF);
        if (intf & CANINTF_TX0IF)
        {
            mcp_bit_modify(dev, REG_CANINTF, CANINTF_TX0IF, 0x00);
            return true;
        }
        sleep_us(100);
    }
    return false;
}

void mcp2515_send_start(mcp2515_t *dev, const can_frame_t *frame)
{
    uint8_t buf[14];
    buf[0] = MCP_LOAD_TX0;

    if (frame->ext)
    {
        uint32_t eid = frame->id;
        buf[1] = (uint8_t)(eid >> 21);
        buf[2] = (uint8_t)(((eid >> 13) & 0xE0) | 0x08 |
                           ((eid >> 16) & 0x03));
        buf[3] = (uint8_t)(eid >> 8);
        buf[4] = (uint8_t)(eid);
    }
    else
    {
        buf[1] = (uint8_t)(frame->id >> 3);
        buf[2] = (uint8_t)((frame->id & 0x07) << 5);
        buf[3] = 0x00;
        buf[4] = 0x00;
    }

    uint8_t dlc = frame->dlc & 0x0F;
    if (frame->rtr)
        dlc |= 0x40;
    buf[5] = dlc;

    if (!frame->rtr)
    {
        memcpy(&buf[6], frame->data, frame->dlc & 0x0F);
    }

    cs_select(dev);
    spi_write_blocking(dev->spi, buf, 6 + (frame->rtr ? 0 : (frame->dlc & 0x0F)));
    cs_deselect(dev);

    uint8_t rts = MCP_RTS_TX0;
    cs_select(dev);
    spi_write_blocking(dev->spi, &rts, 1);
    cs_deselect(dev);
}

bool mcp2515_tx_complete(mcp2515_t *dev)
{
    uint8_t stat = mcp2515_quick_status(dev);
    if (stat & MCP_STAT_TX0IF)
    {
        mcp_bit_modify(dev, REG_CANINTF, CANINTF_TX0IF, 0x00);
        return true;
    }
    return false;
}
void mcp2515_load_tx_buf(mcp2515_t *dev, uint8_t buf_idx,
                         const can_frame_t *frame)
{
    static const uint8_t load_cmd[3] = {MCP_LOAD_TX0, MCP_LOAD_TX1, MCP_LOAD_TX2};
    static const uint8_t rts_cmd[3] = {MCP_RTS_TX0, MCP_RTS_TX1, MCP_RTS_TX2};

    uint8_t buf[14];
    buf[0] = load_cmd[buf_idx];

    if (frame->ext)
    {
        uint32_t eid = frame->id;
        buf[1] = (uint8_t)(eid >> 21);
        buf[2] = (uint8_t)(((eid >> 13) & 0xE0) | 0x08 |
                           ((eid >> 16) & 0x03));
        buf[3] = (uint8_t)(eid >> 8);
        buf[4] = (uint8_t)(eid);
    }
    else
    {
        buf[1] = (uint8_t)(frame->id >> 3);
        buf[2] = (uint8_t)((frame->id & 0x07) << 5);
        buf[3] = 0x00;
        buf[4] = 0x00;
    }

    uint8_t dlc = frame->dlc & 0x0F;
    if (frame->rtr)
        dlc |= 0x40;
    buf[5] = dlc;

    if (!frame->rtr)
        memcpy(&buf[6], frame->data, frame->dlc & 0x0F);

    cs_select(dev);
    spi_write_blocking(dev->spi, buf, 6 + (frame->rtr ? 0 : (frame->dlc & 0x0F)));
    cs_deselect(dev);

    uint8_t rts = rts_cmd[buf_idx];
    cs_select(dev);
    spi_write_blocking(dev->spi, &rts, 1);
    cs_deselect(dev);
}
/* Read a frame from an RX buffer using the quick-read instruction */
static bool mcp_read_rx_buf(mcp2515_t *dev, uint8_t read_cmd,
                            uint8_t intf_flag, can_frame_t *frame)
{
    uint8_t tx[14];
    uint8_t rx[14];
    memset(tx, 0, sizeof(tx));
    tx[0] = read_cmd;

    cs_select(dev);
    spi_write_read_blocking(dev->spi, tx, rx, 14);
    cs_deselect(dev);

    /* rx[1]=SIDH, rx[2]=SIDL, rx[3]=EID8, rx[4]=EID0, rx[5]=DLC */
    uint8_t sidl = rx[2];
    bool ext = (sidl & 0x08) != 0; /* EXIDE bit */

    if (ext)
    {
        /* Extended ID: combine SIDH, SIDL, EID8, EID0 */
        uint32_t id = 0;
        id |= (uint32_t)rx[1] << 21;         /* SIDH -> ID[28:21] */
        id |= (uint32_t)(sidl & 0xE0) << 13; /* SIDL[7:5] -> ID[20:18] */
        id |= (uint32_t)(sidl & 0x03) << 16; /* SIDL[1:0] -> ID[17:16] */
        id |= (uint32_t)rx[3] << 8;          /* EID8 -> ID[15:8] */
        id |= (uint32_t)rx[4];               /* EID0 -> ID[7:0] */
        frame->id = id;
        frame->ext = 1;
    }
    else
    {
        frame->id = ((uint32_t)rx[1] << 3) | ((rx[2] >> 5) & 0x07);
        frame->ext = 0;
    }

    frame->dlc = rx[5] & 0x0F;
    if (frame->dlc > 8)
        frame->dlc = 8;
    frame->rtr = (rx[5] & 0x40) ? 1 : 0;

    if (!frame->rtr)
    {
        memcpy(frame->data, &rx[6], frame->dlc);
    }
    else
    {
        memset(frame->data, 0, 8);
    }

    /* The READ_RX instruction (0x90/0x94) auto-clears the interrupt flag.
     * Do NOT issue an explicit mcp_bit_modify() here: there is a race
     * where a new frame could set the flag between the auto-clear and
     * our manual clear, causing us to lose that frame's interrupt. */

    return true;
}

bool mcp2515_receive(mcp2515_t *dev, can_frame_t *frame)
{
    uint8_t stat = mcp2515_quick_status(dev);

    if (stat & MCP_STAT_RX0IF)
        return mcp_read_rx_buf(dev, MCP_READ_RX0, CANINTF_RX0IF, frame);

    if (stat & MCP_STAT_RX1IF)
        return mcp_read_rx_buf(dev, MCP_READ_RX1, CANINTF_RX1IF, frame);

    return false;
}

uint8_t mcp2515_status(mcp2515_t *dev)
{
    return mcp2515_read_reg(dev, REG_CANSTAT);
}

void mcp2515_read_errors(mcp2515_t *dev, mcp2515_errors_t *err)
{
    err->tec = mcp2515_read_reg(dev, REG_TEC);
    err->rec = mcp2515_read_reg(dev, REG_REC);
    err->eflg = mcp2515_read_reg(dev, REG_EFLG);
}

static bool mcp2515_set_mode(mcp2515_t *dev, uint8_t mode)
{
    mcp_bit_modify(dev, REG_CANCTRL, MODE_MASK, mode);
    sleep_ms(10);
    uint8_t stat = mcp2515_read_reg(dev, REG_CANSTAT);
    return (stat & MODE_MASK) == mode;
}

bool mcp2515_set_loopback(mcp2515_t *dev)
{
    return mcp2515_set_mode(dev, MODE_LOOPBACK);
}

bool mcp2515_set_normal(mcp2515_t *dev)
{
    return mcp2515_set_mode(dev, MODE_NORMAL);
}

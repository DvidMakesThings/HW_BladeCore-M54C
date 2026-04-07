/*
 * mcp2515.c - Minimal MCP2515 SPI CAN controller driver for RP2350B
 */

#include "mcp2515.h"
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

/* ---- Public API -------------------------------------------------------- */

bool mcp2515_init(mcp2515_t *dev)
{
    /* --- GPIO setup --- */
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

    /* --- SPI setup --- */
    spi_init(dev->spi, 10 * 1000 * 1000); /* 10 MHz */
    spi_set_format(dev->spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(dev->pin_miso, GPIO_FUNC_SPI);
    gpio_set_function(dev->pin_mosi, GPIO_FUNC_SPI);
    gpio_set_function(dev->pin_sck, GPIO_FUNC_SPI);

    /* Software reset */
    mcp_reset(dev);
    sleep_ms(10);

    /* Verify we are in configuration mode */
    uint8_t stat = mcp2515_read_reg(dev, REG_CANSTAT);
    if ((stat & MODE_MASK) != MODE_CONFIG)
    {
        return false;
    }

    /* --- Bit timing: 1 Mbps @ 16 MHz oscillator --- */
    mcp_write_reg(dev, REG_CNF1, CNF1_1MBPS);
    mcp_write_reg(dev, REG_CNF2, CNF2_1MBPS);
    mcp_write_reg(dev, REG_CNF3, CNF3_1MBPS);

    /* Enable RX0 interrupt, accept all messages (no filters) */
    mcp_write_reg(dev, REG_CANINTE, CANINTF_RX0IF);
    mcp_write_reg(dev, REG_RXB0CTRL, 0x60); /* RXM=11: turn off masks/filters */

    /* Clear all interrupt flags */
    mcp_write_reg(dev, REG_CANINTF, 0x00);

    /* Switch to normal mode */
    mcp_bit_modify(dev, REG_CANCTRL, MODE_MASK, MODE_NORMAL);
    sleep_ms(10);

    stat = mcp2515_read_reg(dev, REG_CANSTAT);
    if ((stat & MODE_MASK) != MODE_NORMAL)
    {
        return false;
    }

    return true;
}

bool mcp2515_send(mcp2515_t *dev, const can_frame_t *frame)
{
    /* Load TX buffer 0 via LOAD_TX0 instruction (starts at TXB0SIDH) */
    uint8_t buf[14];
    buf[0] = MCP_LOAD_TX0;
    buf[1] = (uint8_t)(frame->id >> 3);          /* SIDH */
    buf[2] = (uint8_t)((frame->id & 0x07) << 5); /* SIDL (standard, no ext) */
    buf[3] = 0x00;                               /* EID8 */
    buf[4] = 0x00;                               /* EID0 */
    buf[5] = frame->dlc & 0x0F;                  /* DLC */
    memcpy(&buf[6], frame->data, frame->dlc);

    cs_select(dev);
    spi_write_blocking(dev->spi, buf, 6 + frame->dlc);
    cs_deselect(dev);

    /* Request to send */
    uint8_t rts = MCP_RTS_TX0;
    cs_select(dev);
    spi_write_blocking(dev->spi, &rts, 1);
    cs_deselect(dev);

    /* Wait for TX complete (TX0IF set) or timeout */
    for (int i = 0; i < 100; i++)
    {
        uint8_t intf = mcp2515_read_reg(dev, REG_CANINTF);
        if (intf & CANINTF_TX0IF)
        {
            mcp_bit_modify(dev, REG_CANINTF, CANINTF_TX0IF, 0x00);
            return true;
        }
        sleep_ms(1);
    }
    return false;
}

bool mcp2515_receive(mcp2515_t *dev, can_frame_t *frame)
{
    uint8_t intf = mcp2515_read_reg(dev, REG_CANINTF);
    if (!(intf & CANINTF_RX0IF))
    {
        return false;
    }

    /* Read RX buffer 0 via READ_RX0 instruction */
    uint8_t cmd = MCP_READ_RX0;
    uint8_t tx[14];
    uint8_t rx[14];
    memset(tx, 0, sizeof(tx));
    tx[0] = cmd;

    cs_select(dev);
    spi_write_read_blocking(dev->spi, tx, rx, 14);
    cs_deselect(dev);

    /* Parse: rx[1]=SIDH, rx[2]=SIDL, rx[3]=EID8, rx[4]=EID0, rx[5]=DLC */
    frame->id = ((uint32_t)rx[1] << 3) | ((rx[2] >> 5) & 0x07);
    frame->dlc = rx[5] & 0x0F;
    if (frame->dlc > 8)
        frame->dlc = 8;
    memcpy(frame->data, &rx[6], frame->dlc);

    /* Clear RX0 interrupt flag */
    mcp_bit_modify(dev, REG_CANINTF, CANINTF_RX0IF, 0x00);

    return true;
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

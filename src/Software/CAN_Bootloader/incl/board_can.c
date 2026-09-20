/**
 * @file src/Software/CAN_Bootloader/incl/board_can.c
 *
 * @version 1.0.0
 * @date 2026-09-18
 *
 * @details
 * Implements MCP2515 reset, timing setup, standard and extended frame decoding, bounded
 * transmission, and controller diagnostics over SPI1. All board pins come from root CONFIG.h.
 * The register interface matches the controller used by the eWald reference, but this module
 * has no eWald application dependency. Identity comes from the RP2354 silicon ID, and a
 * checked watchdog scratch request enters the independent bootloader.
 *
 * @project CAN_Bootloader - BladeCore-M54C firmware
 * @github https://github.com/DvidMakesThings/HW_BladeCore-M54C
 */
#include "board_can.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "hardware/spi.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include <string.h>

/**
 * @brief Execute an instruction under a single chip-select assertion.
 *
 * @param[in] transmit Command and outgoing bytes.
 * @param[out] receive Incoming bytes, or NULL for a write-only transaction.
 * @param[in] length Number of SPI bytes to transfer.
 * @return None (void).
 *
 * @details
 * Holds the manually controlled MCP2515 chip-select signal low for the complete instruction. A
 * non-NULL receive buffer selects full-duplex SPI; otherwise only the transmit bytes are
 * written. Chip select is released before returning. The caller owns SPI1 exclusively and must
 * not call this helper from a competing interrupt context.
 */
static void transfer(const uint8_t *transmit, uint8_t *receive, size_t length)
{
    gpio_put(PIN_CAN_CS, 0);
    if (receive != NULL)
    {
        spi_write_read_blocking(CAN_SPI_INSTANCE, transmit, receive, length);
    }
    else
    {
        spi_write_blocking(CAN_SPI_INSTANCE, transmit, length);
    }
    gpio_put(PIN_CAN_CS, 1);
}

/**
 * @brief Read one MCP2515 register.
 *
 * @param[in] address Register address.
 * @return Current register contents.
 *
 * @details
 * Sends the MCP2515 READ opcode, register address, and one dummy byte in one chip-select
 * transaction. The first two received bytes are discarded and the final byte is returned. No
 * register side effects are introduced beyond those defined by the controller datasheet.
 */
static uint8_t read_register(uint8_t address)
{
    const uint8_t transmit[3] = {0x03, address, 0}; /**< READ transaction bytes. */
    uint8_t receive[3]; /**< SPI reply with the register value in its final byte. */
    transfer(transmit, receive, sizeof(transmit));
    return receive[2];
}

/**
 * @brief Write one MCP2515 register.
 *
 * @param[in] address Register address.
 * @param[in] value New register contents.
 * @return None (void).
 *
 * @details
 * Sends the MCP2515 WRITE opcode followed by a register address and replacement value. The
 * three bytes are transferred under one chip-select assertion. Configuration-mode and register
 * access restrictions are the responsibility of the higher-level initialisation sequence.
 */
static void write_register(uint8_t address, uint8_t value)
{
    const uint8_t transmit[3] = {0x02, address, value}; /**< WRITE transaction bytes. */
    transfer(transmit, NULL, sizeof(transmit));
}

/**
 * @brief Atomically modify selected bits in a supported register.
 *
 * @param[in] address Register address supporting BIT MODIFY.
 * @param[in] mask Bits to update.
 * @param[in] value Values for the selected bits.
 * @return None (void).
 *
 * @details
 * Sends the MCP2515 BIT MODIFY opcode, register address, mask, and replacement bits under one
 * chip-select assertion. Only mask-selected bits are changed. This helper is used for
 * interrupt acknowledgement and transmit cancellation without overwriting unrelated register
 * bits.
 */
static void modify_register(uint8_t address, uint8_t mask, uint8_t value)
{
    const uint8_t transmit[4] = {0x05, address, mask, value}; /**< BIT MODIFY transaction. */
    transfer(transmit, NULL, sizeof(transmit));
}

/**
 * @brief Initialise SPI1 and reset the MCP2515 at the configured CAN bitrate.
 *
 * @param[in] loopback True selects internal controller loopback instead of the bus.
 * @return True when the controller confirms the requested mode; false otherwise.
 *
 * @details
 * Initialises the GPIO assignments from root CONFIG.h, asserts controller reset, and
 * configures SPI mode 0. After oscillator startup it writes the CAN timing registers, enables
 * both receive buffers with rollover, and requests either normal or loopback mode. Mode
 * confirmation is bounded by CAN_MODE_TIMEOUT_MS. Call only from the single SPI owner; the
 * routine resets pending controller traffic.
 */
bool board_can_init(bool loopback)
{
    /* Set inactive CS before enabling the output to avoid unintended instructions. */
    gpio_init(PIN_CAN_CS);
    gpio_put(PIN_CAN_CS, 1);
    gpio_set_dir(PIN_CAN_CS, GPIO_OUT);
    gpio_init(PIN_CAN_RST);
    gpio_put(PIN_CAN_RST, 0);
    gpio_set_dir(PIN_CAN_RST, GPIO_OUT);
    gpio_init(PIN_CAN_INT);
    gpio_set_dir(PIN_CAN_INT, GPIO_IN);
    gpio_pull_up(PIN_CAN_INT);
    spi_init(CAN_SPI_INSTANCE, CAN_SPI_BAUDRATE);
    spi_set_format(CAN_SPI_INSTANCE, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(PIN_CAN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CAN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CAN_SCK, GPIO_FUNC_SPI);

    /* Complete reset and allow the 16 MHz controller oscillator to settle. */
    sleep_ms(2);
    gpio_put(PIN_CAN_RST, 1);
    sleep_ms(10);
    const uint8_t reset = 0xc0; /**< MCP2515 RESET instruction. */
    transfer(&reset, NULL, 1);
    sleep_ms(10);
    if ((read_register(0x0e) & 0xe0) != 0x80)
    {
        return false;
    }

    write_register(0x2a, CAN_CNF1);
    write_register(0x29, CAN_CNF2);
    write_register(0x28, CAN_CNF3);
    write_register(0x60, 0x64); /* RXB0 accepts both frame formats and rolls into RXB1. */
    write_register(0x70, 0x60);
    write_register(0x2b, 0x03); /* Enable both receive interrupt sources. */
    write_register(0x2c, 0x00);

    const uint8_t mode = loopback ? 0x40 : 0; /**< Requested operating mode. */
    const absolute_time_t deadline =
        make_timeout_time_ms(CAN_MODE_TIMEOUT_MS); /**< Mode deadline. */
    write_register(0x0f, mode | 0x08); /* One-shot mode prevents endless retransmission. */
    do
    {
        if ((read_register(0x0e) & 0xe0) == mode)
        {
            return true;
        }
    } while (!time_reached(deadline));
    return false;
}

/**
 * @brief Read one available receive buffer without waiting for traffic.
 *
 * @param[out] frame Frame destination, written only if a frame is available.
 * @return True if a receive buffer was consumed; false otherwise.
 *
 * @details
 * Checks RX0 and RX1 pending flags, drains the first available buffer, and decodes standard or
 * extended identifiers. The READ RX BUFFER instruction clears that buffer interrupt when chip
 * select rises. DLC is capped at eight bytes, and remote frames are identified for the higher-
 * level dispatcher. The function never waits for a frame and must run in the single SPI owner
 * context.
 */
bool board_can_receive(can_frame_t *frame)
{
    const uint8_t flags = read_register(0x2c);                      /**< Pending receive flags. */
    const uint8_t buffer = (flags & 1) ? 1 : ((flags & 2) ? 2 : 0); /**< Buffer to drain. */
    if (buffer == 0)
    {
        return false;
    }
    uint8_t transmit[14] = {buffer == 1 ? 0x90 : 0x94}; /**< READ RX BUFFER instruction. */
    uint8_t receive[14]; /**< Register header and eight payload bytes. */
    transfer(transmit, receive, sizeof(transmit));

    /* READ RX BUFFER clears its interrupt flag on the CS rising edge. */
    frame->extended = (receive[2] & 8) != 0;
    frame->id = ((uint32_t)receive[1] << 3) | (receive[2] >> 5);
    if (frame->extended)
    {
        frame->id = (frame->id << 18) | ((uint32_t)(receive[2] & 3) << 16) |
                    ((uint32_t)receive[3] << 8) | receive[4];
    }
    frame->rtr = frame->extended ? (receive[5] & 0x40) != 0 : (receive[2] & 0x10) != 0;
    frame->dlc = receive[5] & 15;
    if (frame->dlc > 8)
    {
        frame->dlc = 8;
    }
    memcpy(frame->data, receive + 6, sizeof(frame->data));
    return true;
}

/**
 * @brief Send one frame with a bounded completion wait.
 *
 * @param[in] frame Frame to transmit; DLC must not exceed eight.
 * @return True only after successful transmission is reported by the controller; false otherwise.
 *
 * @details
 * Validates the identifier and DLC before loading transmit buffer zero. A previous completion
 * flag is cleared before the request-to-send command. The controller uses one-shot mode, and
 * the function waits at most CAN_TX_TIMEOUT_MS for successful transmission; an unacknowledged
 * frame is aborted. Bus arbitration loss or missing acknowledgement is reported to the caller
 * for bounded retry.
 */
bool board_can_send(const can_frame_t *frame)
{
    if (frame->dlc > 8 || frame->id > (frame->extended ? 0x1fffffffu : 0x7ffu))
    {
        return false;
    }
    if (read_register(0x30) & 8)
    {
        return false; /* Never overwrite a transmitter-owned buffer. */
    }
    uint8_t transmit[14] = {0x40}; /**< LOAD TX BUFFER 0 instruction and encoded frame. */
    modify_register(0x2c, 4, 0);
    if (frame->extended)
    {
        transmit[1] = frame->id >> 21;
        transmit[2] = ((frame->id >> 13) & 0xe0) | 8 | ((frame->id >> 16) & 3);
        transmit[3] = frame->id >> 8;
        transmit[4] = frame->id;
    }
    else
    {
        transmit[1] = frame->id >> 3;
        transmit[2] = (frame->id & 7) << 5;
    }
    transmit[5] = frame->dlc | (frame->rtr ? 0x40 : 0);
    memcpy(transmit + 6, frame->data, frame->dlc);
    transfer(transmit, NULL, 6 + frame->dlc);

    const uint8_t request = 0x81; /**< Request transmission from TX buffer 0. */
    const absolute_time_t deadline = make_timeout_time_ms(CAN_TX_TIMEOUT_MS); /**< TX deadline. */
    transfer(&request, NULL, 1);
    while (!time_reached(deadline))
    {
        if (!(read_register(0x30) & 8))
        {
            return (read_register(0x2c) & 4) != 0;
        }
    }
    modify_register(0x30, 8, 0); /* Abort a frame that cannot be acknowledged. */
    return false;
}

/**
 * @brief Read the MCP2515 error flags.
 *
 * @par Parameters
 * None (void).
 * @return Contents of EFLG, including receive overflow and bus-off flags.
 *
 * @details
 * Reads the MCP2515 EFLG register through SPI without modifying the controller state. The
 * caller can record bus-off, error-passive, and receive-overflow conditions before applying
 * its own recovery policy. This function is not safe to call concurrently with other SPI
 * controller operations.
 */
uint8_t board_can_errors(void)
{
    return read_register(0x2d);
}

/**
 * @brief Clear receive overflow flags after recording the event.
 *
 * @par Parameters
 * None (void).
 * @return None (void).
 *
 * @details
 * Uses MCP2515 BIT MODIFY to clear only RX0OVR and RX1OVR in EFLG. Other error indicators are
 * preserved. Record the overflow in diagnostics first, because clearing these flags
 * acknowledges that one or more frames may have been lost.
 */
void board_can_clear_overflow(void)
{
    modify_register(0x2d, 0xc0, 0);
}

/**
 * @brief Check SPI and both CAN frame directions using internal loopback.
 *
 * @par Parameters
 * None (void).
 * @return True if identifier, DLC, and payload match the transmitted test frame; false otherwise.
 *
 * @details
 * Resets the controller into internal loopback and sends a known eight-byte extended frame.
 * Identifier, frame format, DLC, and all payload bytes must match on receive. No test traffic
 * reaches the physical CAN bus. The function leaves the controller in loopback, so normal
 * initialisation must follow regardless of the test result.
 */
bool board_can_selftest(void)
{
    if (!board_can_init(true))
    {
        return false;
    }
    /** @brief Known extended frame kept off the physical bus. */
    const can_frame_t transmit = {.id = CAN_REQUEST | 0x1234,
                                  .data = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0},
                                  .dlc = 8,
                                  .extended = true};
    can_frame_t receive; /**< Frame returned by the internal loopback path. */
    return board_can_send(&transmit) && board_can_receive(&receive) && receive.extended &&
           !receive.rtr && receive.id == transmit.id && receive.dlc == 8 &&
           memcmp(receive.data, transmit.data, sizeof(receive.data)) == 0;
}

/**
 * @brief Read the RP2354 silicon ID and derive a stable sixteen-bit address.
 *
 * @param[out] uid Eight-byte destination for the complete identifier.
 * @return Address from one through 65534; zero and 65535 are reserved.
 *
 * @details
 * Uses the Pico SDK identity service, which reads the RP2350 silicon identifier through the
 * boot ROM. The complete eight-byte identifier is copied to the caller and reduced by IEEE
 * CRC-32 into the inclusive address range 1..65534. Zero is reserved for discovery.
 * Destructive requests additionally select the complete UID because shortened addresses can
 * collide.
 */
uint16_t board_identity(uint8_t uid[8])
{
    pico_unique_board_id_t identity; /**< RP2354 silicon identifier supplied by the ROM API. */
    pico_get_unique_board_id(&identity);
    memcpy(uid, identity.id, 8);
    /* Full-UID selection resolves possible collisions in the shortened address. */
    return (uint16_t)(1u + crc32(uid, 8) % 65534u);
}

/**
 * @brief Request recovery through checked watchdog scratch values and reset.
 *
 * @par Parameters
 * None (void).
 * @return None (void); this function never returns.
 *
 * @details
 * Stores a magic value and its complement in watchdog scratch registers zero and one, then
 * requests a watchdog reset. The resident bootloader consumes and clears the pair at startup.
 * This resets out of FreeRTOS before the SRAM-resident flash updater runs, preventing
 * application tasks from accessing XIP during erase or programming.
 */
void board_request_bootloader(void)
{
    /* A complementary pair distinguishes a request from stale scratch contents. */
    watchdog_hw->scratch[0] = BOOT_REQUEST_MAGIC;
    watchdog_hw->scratch[1] = ~BOOT_REQUEST_MAGIC;
    watchdog_reboot(0, 0, 10);
    while (true)
    {
        tight_loop_contents();
    }
}

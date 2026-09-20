/**
 * @file src/Software/CAN_Bootloader/incl/board_can.h
 *
 * @version 1.0.0
 * @date 2026-09-18
 *
 * @details
 * Defines the single-owner interface to the MCP2515 CAN controller and RP2354 silicon identity
 * services. The application calls it from its CAN task and the bootloader calls it from its
 * recovery loop. SPI access is deliberately excluded from the GPIO ISR. Internal loopback
 * diagnostics and bounded transmission waits support commissioning without a serial console.
 *
 * @project CAN_Bootloader - BladeCore-M54C firmware
 * @github https://github.com/DvidMakesThings/HW_BladeCore-M54C
 */
#pragma once
#include "protocol.h"

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
bool board_can_init(bool loopback);

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
bool board_can_receive(can_frame_t *frame);

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
bool board_can_send(const can_frame_t *frame);

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
uint8_t board_can_errors(void);

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
void board_can_clear_overflow(void);

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
bool board_can_selftest(void);

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
uint16_t board_identity(uint8_t uid[8]);

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
void board_request_bootloader(void);

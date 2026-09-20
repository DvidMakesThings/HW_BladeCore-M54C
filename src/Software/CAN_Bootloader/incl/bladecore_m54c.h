/**
 * @file src/Software/CAN_Bootloader/incl/bladecore_m54c.h
 *
 * @version 1.0.0
 * @date 2026-09-18
 *
 * @details
 * Adapts the generated root CONFIG.h to the Pico SDK board-header mechanism. The RP2354B
 * package exposes GPIO0 through GPIO47, including the MCP2515 control pins and heartbeat LED.
 * This wrapper introduces no pin definitions or configuration macros of its own.
 *
 * @project CAN_Bootloader - BladeCore-M54C firmware
 * @github https://github.com/DvidMakesThings/HW_BladeCore-M54C
 */
#pragma once
#include "../CONFIG.h"

/**
 * @file src/Software/BaReTOS/CONFIG.h
 *
 * @version 1.0.0
 * @date 2026-09-18
 *
 * @details
 * Owns the board pin map, peripheral instances, application task priorities and timing, CAN
 * protocol identifiers, and application flash-layout macros for BladeCore-M54C, so the firmware
 * and host tools share one configuration source. FreeRTOS kernel configuration lives separately
 * in FreeRTOSConfig.h. This file is macro-only and safe for the Pico SDK board adapter's
 * assembler preprocessing.
 *
 * @project BaReTOS - BladeCore-M54C firmware
 * @github https://github.com/DvidMakesThings/HW_BladeCore-M54C
 */

#pragma once
#include "FreeRTOSConfig.h"

/* -------------------------------------------------------------------------- */
/*  CAN bus speed selector                                                    */
/* -------------------------------------------------------------------------- */
#define CAN_SPEED_1M 1000u
#define CAN_SPEED_500K 500u
#define CAN_SPEED_250K 250u
#define CAN_SPEED_125K 125u
#define CAN_SPEED CAN_SPEED_500K /**< Active CAN bitrate in kbit/s. */

/* -------------------------------------------------------------------------- */
/*  MCP2515 CAN Controller (SPI1)                                             */
/* -------------------------------------------------------------------------- */
#define PIN_CAN_MISO 28 /**< SPI1 RX  - MCP2515 SO             */
#define PIN_CAN_CS 29   /**< SPI1 CSn - MCP2515 CS (active low)*/
#define PIN_CAN_SCK 30  /**< SPI1 SCK - MCP2515 SCK            */
#define PIN_CAN_MOSI 31 /**< SPI1 TX  - MCP2515 SI             */

/* -------------------------------------------------------------------------- */
/*  I2C0 - Onboard EEPROM (AT24C256) + M.2 connector                         */
/* -------------------------------------------------------------------------- */
#define PIN_I2C0_SDA 32 /**< I2C0 data  (4.7K pull-up)         */
#define PIN_I2C0_SCL 33 /**< I2C0 clock (4.7K pull-up)         */

/* -------------------------------------------------------------------------- */
/*  MCP2515 CAN Control                                                       */
/* -------------------------------------------------------------------------- */
#define PIN_CAN_RST 34 /**< MCP2515 hardware reset (active low)*/
#define PIN_CAN_INT 35 /**< MCP2515 interrupt (active low)    */

/* -------------------------------------------------------------------------- */
/*  Onboard Heartbeat LED                                                     */
/* -------------------------------------------------------------------------- */
#define PIN_HEARTBEAT 36 /**< Blue LED, 100R series resistor    */

/* -------------------------------------------------------------------------- */
/*  ADC - Onboard                                                             */
/* -------------------------------------------------------------------------- */
#define PIN_ADC_VUSB 46 /**< GPIO46/ADC6 - USB VBUS sense (5.1K-5.1K divider) */
#define PIN_ADC_VREF 47 /**< GPIO47/ADC7 - 3.00V 0.1% ref (10K-10K divider)   */
/** @brief Measured true voltage at the VREF ADC pin (3.004 V ref through the 2:1 divider). */
#define ADC_CAL_PIN_MV 1500u

/* -------------------------------------------------------------------------- */
/*  Unused GPIOs - M.2 Connector (directly access through M.2 edge connector) */
/* -------------------------------------------------------------------------- */
/*  Left side (odd pins)                                                      */
// #define PIN_GPIO0             0       /**< M.2 pin 57             */
// #define PIN_GPIO1             1       /**< M.2 pin 55             */
// #define PIN_GPIO2             2       /**< M.2 pin 53             */
// #define PIN_GPIO3             3       /**< M.2 pin 51             */
// #define PIN_GPIO4             4       /**< M.2 pin 49             */
// #define PIN_GPIO5             5       /**< M.2 pin 47             */
// #define PIN_GPIO6             6       /**< M.2 pin 45             */
// #define PIN_GPIO7             7       /**< M.2 pin 43             */
// #define PIN_GPIO8             8       /**< M.2 pin 39             */
// #define PIN_GPIO9             9       /**< M.2 pin 37             */
// #define PIN_GPIO10            10      /**< M.2 pin 35             */
// #define PIN_GPIO11            11      /**< M.2 pin 33             */
// #define PIN_GPIO12            12      /**< M.2 pin 31             */
// #define PIN_GPIO13            13      /**< M.2 pin 29             */
// #define PIN_GPIO14            14      /**< M.2 pin 27             */
// #define PIN_GPIO15            15      /**< M.2 pin 25             */
// #define PIN_GPIO16            16      /**< M.2 pin 21             */
// #define PIN_GPIO17            17      /**< M.2 pin 19             */
// #define PIN_GPIO18            18      /**< M.2 pin 17             */
// #define PIN_GPIO19            19      /**< M.2 pin 15             */
// #define PIN_GPIO20            20      /**< M.2 pin 13             */
// #define PIN_GPIO21            21      /**< M.2 pin 9              */
// #define PIN_GPIO22            22      /**< M.2 pin 7              */
// #define PIN_GPIO23            23      /**< M.2 pin 5              */
// #define PIN_GPIO24            24      /**< M.2 pin 3              */
/*  Right side (even pins)                                                    */
// #define PIN_GPIO25            25      /**< M.2 pin 8              */
// #define PIN_GPIO26            26      /**< M.2 pin 6              */
// #define PIN_GPIO27            27      /**< M.2 pin 4              */
// #define PIN_GPIO37            37      /**< M.2 pin 34             */
// #define PIN_GPIO38            38      /**< M.2 pin 36             */
// #define PIN_GPIO39            39      /**< M.2 pin 40             */
// #define PIN_ADC0              40      /**< M.2 pin 46 / ADC0      */
// #define PIN_ADC1              41      /**< M.2 pin 48 / ADC1      */
// #define PIN_ADC2              42      /**< M.2 pin 50 / ADC2      */
// #define PIN_ADC3              43      /**< M.2 pin 52 / ADC3      */
// #define PIN_ADC4              44      /**< M.2 pin 54 / ADC4      */
// #define PIN_ADC5              45      /**< M.2 pin 56 / ADC5      */
/*  CAN bus signals (directly from TCAN1044, no GPIO)                         */
// CAN_P                                /* M.2 pin 28             */
// CAN_N                                /* M.2 pin 30             */

/* -------------------------------------------------------------------------- */
/*  SPI1 instance used by MCP2515                                             */
/* -------------------------------------------------------------------------- */
/** @brief SPI peripheral wired to the MCP2515. */
#define CAN_SPI_INSTANCE spi1
#define CAN_SPI_BAUDRATE (10 * 1000 * 1000) /**< 10 MHz               */

/* -------------------------------------------------------------------------- */
/*  I2C0 instance used by EEPROM                                              */
/* -------------------------------------------------------------------------- */
/** @brief I2C peripheral wired to the onboard EEPROM. */
#define EEPROM_I2C_INSTANCE i2c0
#define EEPROM_I2C_ADDR 0x50             /**< AT24C256 base address (A0=A1=GND) */
#define EEPROM_I2C_BAUDRATE (400 * 1000) /**< 400 kHz             */

/* -------------------------------------------------------------------------- */
/*  Heartbeat LED - PWM configuration                                         */
/* -------------------------------------------------------------------------- */
/*  GPIO36 -> PWM slice 2, channel A (RP2354B: slice = (gpio >> 1) & 0xF)    */
/** @brief Generator PWM example frequency; the RTOS heartbeat uses GPIO toggling. */
#define HEARTBEAT_PWM_FREQ_HZ 1000
/** @brief Generator PWM example fade interval; unused by the GPIO heartbeat. */
#define HEARTBEAT_FADE_STEP_MS 8

/* -------------------------------------------------------------------------- */
/*  Application task priorities                                                */
/* -------------------------------------------------------------------------- */
#define HEARTBEAT_TASK_PRIORITY tskIDLE_PRIORITY     /**< Minimum scheduler priority. */
#define CAN_TASK_PRIORITY (tskIDLE_PRIORITY + 2)     /**< CAN service task priority. */
#define EXAMPLE_TASK_PRIORITY (tskIDLE_PRIORITY + 1) /**< Example template task priority. */
#define INIT_TASK_PRIORITY (tskIDLE_PRIORITY + 3)    /**< Startup init task; highest, runs first. */

/** @name SDK board configuration
 * @{ */
#define PICO_RP2350A 0                    /**< RP2354B has 48 GPIO pins. */
#define PICO_FLASH_SIZE_BYTES 0x200000    /**< Program flash partition capacity. */
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1 /**< Winbond-compatible XIP setup. */
#define PICO_FLASH_SPI_CLKDIV 4           /**< Conservative program flash clock divider. */
#define PICO_RP2350_A2_SUPPORTED 1        /**< Support RP2350 A2 silicon. */
/** @} */

/** @name CAN update wire protocol and flash layout
 * @{ */
/* 11-bit identifiers: 4-bit class in bits 10..7, 7-bit node address in bits 6..0. */
#define CAN_REQUEST    0x080u /**< Class 1, control request plus destination. */
#define CAN_RESPONSE   0x100u /**< Class 2, acknowledgement plus source. */
#define CAN_IDENTITY   0x180u /**< Class 3, full silicon identifier response. */
#define CAN_DATA       0x200u /**< Class 4, firmware offset and four data bytes. */
#define CAN_SELECT     0x280u /**< Class 5, select by complete silicon ID. */
#define CAN_ENTER      0x300u /**< Class 6, enter bootloader. */
#define CAN_SDO_RX     0x380u /**< Class 7, SDO request addressed to this node. */
#define CAN_SDO_TX     0x400u /**< Class 8, SDO response produced by this node. */
#define CAN_RPDO       0x480u /**< Class 9, process data consumed by this node. */
#define CAN_TPDO       0x500u /**< Class 10, process data produced by this node. */
#define CAN_CLASS_MASK 0x780u /**< Bits selecting the message class. */
#define CAN_NODE_MASK  0x07fu /**< Bits selecting the node address. */
#define PROTOCOL_VERSION 1u    /**< Wire protocol and persistent manifest version. */
#define BOARD_TYPE 0x4d353443u /**< BladeCore-M54C compatibility marker. */
#define APP_OFFSET 0x20000     /**< Application offset in program flash. */
#define META_OFFSET 0x1f000    /**< Dedicated application validity sector. */
#define APP_MAX_SIZE (PICO_FLASH_SIZE_BYTES - APP_OFFSET) /**< Application capacity. */
#define APP_BASE (0x10000000u + APP_OFFSET)               /**< Application XIP vector address. */
#define META_MAGIC 0x42415245u         /**< Committed image manifest signature. */
#define BOOT_REQUEST_MAGIC 0xb007ca4eu /**< Watchdog scratch recovery request. */

#if CAN_SPEED == CAN_SPEED_1M
#define CAN_CNF1 0x00u /**< 16 MHz oscillator, BRP=0, 1 Mbit/s. */
#elif CAN_SPEED == CAN_SPEED_500K
#define CAN_CNF1 0x01u /**< 16 MHz oscillator, BRP=1, 500 kbit/s. */
#elif CAN_SPEED == CAN_SPEED_250K
#define CAN_CNF1 0x03u /**< 16 MHz oscillator, BRP=3, 250 kbit/s. */
#elif CAN_SPEED == CAN_SPEED_125K
#define CAN_CNF1 0x07u /**< 16 MHz oscillator, BRP=7, 125 kbit/s. */
#else
#error "Unsupported CAN_SPEED value"
#endif
#define CAN_CNF2 0x90u                 /**< Eight time quanta, 62.5 percent sample point. */
#define CAN_CNF3 0x02u                 /**< Phase segment 2 is three time quanta. */
#define CAN_TX_TIMEOUT_MS 3u           /**< Maximum controller transmit wait. */
#define CAN_MODE_TIMEOUT_MS 20u        /**< Maximum operating-mode transition wait. */
#define CAN_DISCOVERY_WINDOW_MS 50u    /**< Spacing window for discovery responses. */
/** @} */

/** @name Task timing and watchdog settings
 * @{ */
#define SYSTEM_CLOCK_KHZ 150000u       /**< RP2354 system clock in kHz. */
#define WATCHDOG_TIMEOUT_MS 3000u      /**< Maximum permitted task stall. */
#define FAULT_RESET_DELAY_MS 100u      /**< Delay before a fault-triggered reset. */
#define HEARTBEAT_PERIOD_MS 500u       /**< Interval between heartbeat LED transitions. */
#define HEARTBEAT_STACK_WORDS 256u     /**< Heartbeat task stack in 32-bit words. */
#define CAN_STACK_WORDS 768u           /**< CAN task stack in 32-bit words. */
#define INIT_STACK_WORDS 256u          /**< Startup init task stack in 32-bit words. */
#define EXAMPLE_PERIOD_MS 1000u        /**< Example template task wake interval. */
#define EXAMPLE_STACK_WORDS 256u       /**< Example template task stack in 32-bit words. */
#define CAN_RX_BUDGET 32u              /**< Maximum receive frames per service pass. */
#define CAN_POLL_MS 5u                 /**< Polling fallback for a missed IRQ edge. */
#define CAN_RETRY_MS 500u              /**< Controller reinitialisation retry interval. */
#define BOOT_SESSION_TIMEOUT_MS 30000u /**< Abandon a stalled firmware transfer. */
#define BOOT_HEARTBEAT_PERIOD_MS 100u  /**< Recovery-mode LED interval. */
/** @} */

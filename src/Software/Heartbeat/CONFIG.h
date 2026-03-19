/*
 * CONFIG.h - BladeCore-M54C Board Pin Configuration
 *
 * Pin assignments derived from the BladeCore-M54C hardware schematics (v1.0.0).
 * MCU: RP2354B (QFN-80)
 */

#ifndef CONFIG_H
#define CONFIG_H

/* -------------------------------------------------------------------------- */
/*  MCP2515 CAN Controller (SPI1)                                             */
/* -------------------------------------------------------------------------- */
#define PIN_CAN_MISO 28 /* SPI1 RX  - MCP2515 SO             */
#define PIN_CAN_CS 29   /* SPI1 CSn - MCP2515 CS (active low)*/
#define PIN_CAN_SCK 30  /* SPI1 SCK - MCP2515 SCK            */
#define PIN_CAN_MOSI 31 /* SPI1 TX  - MCP2515 SI             */

/* -------------------------------------------------------------------------- */
/*  I2C0 - Onboard EEPROM (AT24C256) + M.2 connector                         */
/* -------------------------------------------------------------------------- */
#define PIN_I2C0_SDA 32 /* I2C0 data  (4.7K pull-up)         */
#define PIN_I2C0_SCL 33 /* I2C0 clock (4.7K pull-up)         */

/* -------------------------------------------------------------------------- */
/*  MCP2515 CAN Control                                                       */
/* -------------------------------------------------------------------------- */
#define PIN_CAN_RST 34 /* MCP2515 hardware reset (active low)*/
#define PIN_CAN_INT 35 /* MCP2515 interrupt (active low)    */

/* -------------------------------------------------------------------------- */
/*  Onboard Heartbeat LED                                                     */
/* -------------------------------------------------------------------------- */
#define PIN_HEARTBEAT 36 /* Blue LED, 100R series resistor    */

/* -------------------------------------------------------------------------- */
/*  ADC - Onboard                                                             */
/* -------------------------------------------------------------------------- */
#define PIN_ADC_VUSB 46 /* GPIO46/ADC6 - USB VBUS sense (5.1K-5.1K divider) */
#define PIN_ADC_VREF 47 /* GPIO47/ADC7 - 3.00V 0.1% ref (10K-10K divider)   */

/* -------------------------------------------------------------------------- */
/*  Unused GPIOs - M.2 Connector (directly access through M.2 edge connector) */
/* -------------------------------------------------------------------------- */
/*  Left side (odd pins)                                                      */
// #define PIN_GPIO0             0       /* M.2 pin 57             */
// #define PIN_GPIO1             1       /* M.2 pin 55             */
// #define PIN_GPIO2             2       /* M.2 pin 53             */
// #define PIN_GPIO3             3       /* M.2 pin 51             */
// #define PIN_GPIO4             4       /* M.2 pin 49             */
// #define PIN_GPIO5             5       /* M.2 pin 47             */
// #define PIN_GPIO6             6       /* M.2 pin 45             */
// #define PIN_GPIO7             7       /* M.2 pin 43             */
// #define PIN_GPIO8             8       /* M.2 pin 39             */
// #define PIN_GPIO9             9       /* M.2 pin 37             */
// #define PIN_GPIO10            10      /* M.2 pin 35             */
// #define PIN_GPIO11            11      /* M.2 pin 33             */
// #define PIN_GPIO12            12      /* M.2 pin 31             */
// #define PIN_GPIO13            13      /* M.2 pin 29             */
// #define PIN_GPIO14            14      /* M.2 pin 27             */
// #define PIN_GPIO15            15      /* M.2 pin 25             */
// #define PIN_GPIO16            16      /* M.2 pin 21             */
// #define PIN_GPIO17            17      /* M.2 pin 19             */
// #define PIN_GPIO18            18      /* M.2 pin 17             */
// #define PIN_GPIO19            19      /* M.2 pin 15             */
// #define PIN_GPIO20            20      /* M.2 pin 13             */
// #define PIN_GPIO21            21      /* M.2 pin 9              */
// #define PIN_GPIO22            22      /* M.2 pin 7              */
// #define PIN_GPIO23            23      /* M.2 pin 5              */
// #define PIN_GPIO24            24      /* M.2 pin 3              */
/*  Right side (even pins)                                                    */
// #define PIN_GPIO25            25      /* M.2 pin 8              */
// #define PIN_GPIO26            26      /* M.2 pin 6              */
// #define PIN_GPIO27            27      /* M.2 pin 4              */
// #define PIN_GPIO37            37      /* M.2 pin 34             */
// #define PIN_GPIO38            38      /* M.2 pin 36             */
// #define PIN_GPIO39            39      /* M.2 pin 40             */
// #define PIN_ADC0              40      /* M.2 pin 46 / ADC0      */
// #define PIN_ADC1              41      /* M.2 pin 48 / ADC1      */
// #define PIN_ADC2              42      /* M.2 pin 50 / ADC2      */
// #define PIN_ADC3              43      /* M.2 pin 52 / ADC3      */
// #define PIN_ADC4              44      /* M.2 pin 54 / ADC4      */
// #define PIN_ADC5              45      /* M.2 pin 56 / ADC5      */
/*  CAN bus signals (directly from TCAN1044, no GPIO)                         */
// CAN_P                                /* M.2 pin 28             */
// CAN_N                                /* M.2 pin 30             */

/* -------------------------------------------------------------------------- */
/*  SPI1 instance used by MCP2515                                             */
/* -------------------------------------------------------------------------- */
#define CAN_SPI_INSTANCE spi1
#define CAN_SPI_BAUDRATE (10 * 1000 * 1000) /* 10 MHz               */

/* -------------------------------------------------------------------------- */
/*  I2C0 instance used by EEPROM                                              */
/* -------------------------------------------------------------------------- */
#define EEPROM_I2C_INSTANCE i2c0
#define EEPROM_I2C_ADDR 0x50             /* AT24C256 base address (A0=A1=GND) */
#define EEPROM_I2C_BAUDRATE (400 * 1000) /* 400 kHz             */

/* -------------------------------------------------------------------------- */
/*  Heartbeat LED - PWM configuration                                         */
/* -------------------------------------------------------------------------- */
/*  GPIO36 -> PWM slice 2, channel A (RP2354B: slice = (gpio >> 1) & 0xF)    */
#define HEARTBEAT_PWM_FREQ_HZ 1000
#define HEARTBEAT_FADE_STEP_MS 8

#endif /* CONFIG_H */

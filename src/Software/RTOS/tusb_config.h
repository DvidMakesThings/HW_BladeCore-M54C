/*
 * tusb_config.h - TinyUSB configuration for BladeCore-M54C PCAN adapter
 */

#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

/* Port 0: USB full-speed device */
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

/* FreeRTOS integration (only define if not already set by build system) */
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_FREERTOS
#endif

/* Device stack */
#define CFG_TUD_ENABLED 1
#define CFG_TUD_ENDPOINT0_SIZE 64

/* No built-in classes -- we use a custom class driver for PCAN */
#define CFG_TUD_CDC 0
#define CFG_TUD_HID 0
#define CFG_TUD_MSC 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0
#define CFG_TUD_DFU_RUNTIME 0
#define CFG_TUD_NCM 0
#define CFG_TUD_AUDIO 0
#define CFG_TUD_VIDEO 0

#endif /* TUSB_CONFIG_H */

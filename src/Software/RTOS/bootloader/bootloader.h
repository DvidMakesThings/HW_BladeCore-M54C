/*
 * bootloader.h - CAN firmware update bootloader interface
 */

#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include <stdint.h>
#include <stdbool.h>

/* Persistent trigger word placed in .uninitialized_data.
 * Survives watchdog reboot but not power cycle. */
extern volatile uint32_t bootloader_trigger;

/* Check if bootloader mode was requested (magic word present).
 * Called early in main() before starting FreeRTOS.
 * If triggered, enters CAN firmware update mode (does not return).
 * If not triggered, returns immediately. */
void bootloader_check(void);

/* Schedule a watchdog reboot (used after setting magic word).
 * The reboot happens after a short delay to allow USB status stage. */
void bootloader_schedule_reboot(void);

/* -------------------------------------------------------------------------- */
/*  CAN bootloader protocol                                                    */
/* -------------------------------------------------------------------------- */
/*  Command frames: CAN ID 0x7F0 (to device)                                 */
/*  Response frames: CAN ID 0x7F1 (from device)                              */
/*  Data frames: CAN ID 0x7F2 (to device, firmware chunks)                   */
/*                                                                             */
/*  Commands (data[0]):                                                        */
/*    0x01 BL_START   - Start firmware update. data[1..4] = size (LE32)       */
/*    0x02 BL_DATA    - Firmware data chunk.  data[1..7] = 7 bytes            */
/*    0x03 BL_VERIFY  - Verify CRC32. data[1..4] = expected CRC (LE32)       */
/*    0x04 BL_REBOOT  - Reboot to application                                */
/*    0x05 BL_ERASE   - Erase application flash region                        */
/*    0x06 BL_STATUS  - Query bootloader status                               */
/*                                                                             */
/*  Response (data[0]):                                                        */
/*    0x00 ACK     data[1..] = optional info                                  */
/*    0xFF NAK     data[1] = error code                                       */
/* -------------------------------------------------------------------------- */
#define BL_CMD_ID       0x7F0u
#define BL_RSP_ID       0x7F1u

#define BL_CMD_START    0x01u
#define BL_CMD_DATA     0x02u
#define BL_CMD_VERIFY   0x03u
#define BL_CMD_REBOOT   0x04u
#define BL_CMD_ERASE    0x05u
#define BL_CMD_STATUS   0x06u

#define BL_RSP_ACK      0x00u
#define BL_RSP_NAK      0xFFu

/* CAN trigger: extended ID 0x1FFFFFF0 + payload [B0 07 10 AD B0 07 10 AD] */
#define BL_CAN_TRIGGER_ID   0x7F0u
#define BL_CAN_TRIGGER_CMD  0xB0u

#endif /* BOOTLOADER_H */

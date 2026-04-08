/*
 * pcan_protocol.h - PCAN-USB protocol constants and types
 *
 * Based on the PCAN-USB protocol as documented in the Linux kernel
 * driver (drivers/net/can/usb/peak_usb/pcan_usb.c).
 *
 * NOTE: This implementation is for development/testing only.
 *       Uses PEAK Systems VID/PID (0x0C72/0x000C).
 */

#ifndef PCAN_PROTOCOL_H
#define PCAN_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------- */
/*  USB IDs                                                                    */
/* -------------------------------------------------------------------------- */
#define PCAN_USB_VENDOR_ID 0x0C72u
#define PCAN_USB_PRODUCT_ID 0x000Cu

/* -------------------------------------------------------------------------- */
/*  Endpoint addresses                                                        */
/* -------------------------------------------------------------------------- */
#define PCAN_EP_CMD_OUT 0x01u
#define PCAN_EP_CMD_IN 0x81u
#define PCAN_EP_MSG_OUT 0x02u
#define PCAN_EP_MSG_IN 0x82u

/* -------------------------------------------------------------------------- */
/*  Message / command sizes                                                    */
/* -------------------------------------------------------------------------- */
#define PCAN_USB_MSG_HEADER_LEN 2
#define PCAN_USB_CMD_LEN 16
#define PCAN_USB_CMD_ARGS_LEN 14
#define PCAN_USB_EP_SIZE 64

/* -------------------------------------------------------------------------- */
/*  Command function codes                                                     */
/* -------------------------------------------------------------------------- */
#define PCAN_CMD_BITRATE 1
#define PCAN_CMD_SET_BUS 3
#define PCAN_CMD_DEVID 4
#define PCAN_CMD_SN 6
#define PCAN_CMD_REGISTER 9
#define PCAN_CMD_EXT_VCC 10
#define PCAN_CMD_ERR_FR 11
#define PCAN_CMD_LED 12

/* -------------------------------------------------------------------------- */
/*  Command sub-operations                                                     */
/* -------------------------------------------------------------------------- */
#define PCAN_CMD_GET 1
#define PCAN_CMD_SET 2

/* SET_BUS sub-numbers */
#define PCAN_BUS_XCVER 2
#define PCAN_BUS_SILENT 3

/* -------------------------------------------------------------------------- */
/*  SJA1000 modes (used via REGISTER command)                                  */
/* -------------------------------------------------------------------------- */
#define SJA1000_MODE_NORMAL 0x00
#define SJA1000_MODE_INIT 0x01

/* -------------------------------------------------------------------------- */
/*  Message encoding constants                                                 */
/* -------------------------------------------------------------------------- */
#define PCAN_MSG_TX_CAN 0x02u

/* Status/len byte flags */
#define PCAN_SL_TIMESTAMP (1u << 7)
#define PCAN_SL_INTERNAL (1u << 6)
#define PCAN_SL_EXT_ID (1u << 5)
#define PCAN_SL_RTR (1u << 4)
#define PCAN_SL_DLC_MASK 0x0Fu

/* TX frame flags (in CAN ID word) */
#define PCAN_TX_SRR 0x01u
#define PCAN_TX_AT 0x02u

/* Status record function codes (INTERNAL messages on EP2 IN) */
#define PCAN_USB_REC_ERROR 1u
#define PCAN_USB_REC_TS 4u

/* -------------------------------------------------------------------------- */
/*  Timestamp                                                                  */
/* -------------------------------------------------------------------------- */
/* PCAN-USB tick ~= 42.666 us.  Convert from us: tick = us * 3 / 128 */
#define PCAN_TS_US_TO_TICK_NUM 3
#define PCAN_TS_US_TO_TICK_DEN 128

/* -------------------------------------------------------------------------- */
/*  CAN frame (internal representation)                                        */
/* -------------------------------------------------------------------------- */
typedef struct
{
    uint32_t id;   /* 11-bit standard or 29-bit extended */
    uint8_t dlc;   /* 0..8 */
    uint8_t flags; /* bit0=EFF, bit1=RTR */
    uint8_t data[8];
} pcan_can_frame_t;

#define PCAN_FRAME_FLAG_EFF 0x01u
#define PCAN_FRAME_FLAG_RTR 0x02u

/* -------------------------------------------------------------------------- */
/*  Protocol functions                                                         */
/* -------------------------------------------------------------------------- */

/* Decode a PCAN TX message (host->device) into CAN frames.
 * buf/len: raw USB message on EP_MSG_OUT.
 * frames: output array (caller provides).
 * max_frames: size of output array.
 * Returns number of frames decoded, or -1 on error. */
int pcan_decode_tx_msg(const uint8_t *buf, uint32_t len,
                       pcan_can_frame_t *frames, int max_frames);

/* Encode one or more CAN frames into a PCAN RX message (device->host).
 * buf: output buffer (must be >= PCAN_USB_EP_SIZE).
 * frames/count: CAN frames to encode.
 * Returns number of bytes written to buf. */
int pcan_encode_rx_msg(uint8_t *buf, const pcan_can_frame_t *frames,
                       int count, uint16_t timestamp);

/* Encode a bus-active status message for EP2 IN.
 * Signals ERROR_ACTIVE (healthy bus) to the host driver. */
int pcan_encode_bus_active_msg(uint8_t *buf, uint16_t timestamp);

/* Process a PCAN command received on EP_CMD_OUT.
 * cmd_buf: 16-byte command buffer.
 * resp_buf: 16-byte response buffer (filled if a response is needed).
 * Returns true if a response should be sent on EP_CMD_IN. */
bool pcan_handle_command(const uint8_t *cmd_buf, uint8_t *resp_buf);

/* Notify protocol layer of bitrate change.  Converts BTR0/BTR1 to
 * MCP2515 CNF registers. */
void pcan_get_mcp_timing(uint8_t *cnf1, uint8_t *cnf2, uint8_t *cnf3);

/* Query bus-on state (set by SET_BUS command) */
bool pcan_is_bus_on(void);

/* Get device serial number (derived from flash UID) */
uint32_t pcan_get_serial(void);

#endif /* PCAN_PROTOCOL_H */

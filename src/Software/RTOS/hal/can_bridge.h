/*
 * can_bridge.h - CAN <-> USB bridge task interface
 */

#ifndef CAN_BRIDGE_H
#define CAN_BRIDGE_H

#include "usb/pcan_protocol.h"

/* Create CAN TX and RX FreeRTOS tasks.
 * Must be called after FreeRTOS scheduler init but before vTaskStartScheduler
 * or from another task. */
void can_bridge_create_tasks(void);

/* Enqueue a CAN frame for transmission on the bus (called from USB context). */
void can_bridge_enqueue_tx(const pcan_can_frame_t *frame);

/* Called from USB task context to drain CAN RX queue and send to host.
 * Returns number of frames forwarded. */
int can_bridge_poll_rx(void);

/* Reconfigure MCP2515 with current PCAN bitrate settings.
 * Called when bus transitions to ON. */
void can_bridge_reconfigure(void);

#endif /* CAN_BRIDGE_H */

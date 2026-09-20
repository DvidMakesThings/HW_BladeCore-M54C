/**
 * @file src/Software/BaReTOS/incl/can_router/can_router.h
 *
 * @version 1.0.0
 * @date 2026-09-18
 *
 * @details
 * Declares the CAN message router: the single point that inspects a received frame's message
 * class and forwards it to the matching handler. The router keeps the CAN task free of any
 * per-message-type logic, so new object types (SDO, RPDO, TPDO, ...) are added by editing one
 * dispatch table in can_router.c rather than the receive loop.
 *
 * Extension points:
 *   - SDO objects are served in route_sdo() in can_router.c; add a case per ObjDict.h index.
 *   - application_rpdo_received() strong-override to consume received process data.
 *   - can_tpdo_send()            call from any task to publish process data.
 *
 * @project BaReTOS - BladeCore-M54C firmware
 * @github https://github.com/DvidMakesThings/HW_BladeCore-M54C
 */
#pragma once
#include "board_can.h"

/**
 * @brief Route one received frame to the handler registered for its message class.
 *
 * @param[in] frame Frame consumed from the controller; borrowed for the call only.
 * @return None (void).
 *
 * @details
 * Masks the identifier with CAN_CLASS_MASK and looks the result up in the dispatch table in
 * can_router.c. The first matching handler is invoked and routing stops. Frames whose class is
 * not in the table are ignored. Runs in the CAN task context and may transmit replies through
 * the single-owner board_can interface.
 */
void can_router_dispatch(const can_frame_t *frame);

/**
 * @brief Publish process data as a transmit PDO.
 *
 * @param[in] data Payload bytes to send; may be NULL only when dlc is zero.
 * @param[in] dlc Payload length from zero through eight.
 * @return True when the controller accepted and transmitted the frame; false otherwise.
 *
 * @details
 * Builds an extended frame with the CAN_TPDO class and this node's address, then sends it
 * through the bounded board_can_send routine. Call it from a periodic or event-driven task to
 * emit process data. Additional TPDO classes can be added later for multiple channels.
 */
bool can_tpdo_send(const uint8_t *data, uint8_t dlc);

/**
 * @brief Application hook for a received process-data object (RPDO).
 *
 * @param[in] frame Borrowed RPDO frame, valid only during the call.
 * @return None (void).
 *
 * @details
 * Weak default does nothing. Override it with a strong definition to apply incoming process
 * data. Copy anything that must outlive the call. Keep the handler short and non-blocking.
 */
void application_rpdo_received(const can_frame_t *frame);

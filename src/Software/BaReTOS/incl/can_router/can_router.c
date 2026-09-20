/**
 * @file src/Software/BaReTOS/incl/can_router/can_router.c
 *
 * @version 1.0.0
 * @date 2026-09-18
 *
 * @details
 * Implements the CAN message router. A single dispatch table maps each message class to the
 * handler that services it; the CAN task calls can_router_dispatch for every received frame and
 * nothing else needs to know about individual message types. Adding a new object type is a two-
 * step change local to this file: write a handler and add one row to routes[].
 *
 * Currently registered:
 *   - CAN_REQUEST / CAN_ENTER : device management (identity discovery and bootloader entry).
 *   - CAN_SDO_RX              : SDO requests, forwarded to the application_sdo_request hook.
 *   - CAN_RPDO               : process data, forwarded to the application_rpdo_received hook.
 *
 * @project BaReTOS - BladeCore-M54C firmware
 * @github https://github.com/DvidMakesThings/HW_BladeCore-M54C
 */
#include "can_router.h"
#include "application.h"
#include "ObjDict.h"
#include "adc.h"
#include <string.h>

/**
 * @brief Transmit a frame and record the result in the health counters.
 *
 * @param[in] frame Frame to transmit.
 * @return None (void).
 *
 * @details
 * Delegates to the bounded board_can_send routine and increments either the transmitted or the
 * tx_errors counter. Shared by every handler so transmit accounting stays consistent.
 */
static void send_response(const can_frame_t *frame)
{
    if (board_can_send(frame))
    {
        ++bare_diag.transmitted;
    }
    else
    {
        ++bare_diag.tx_errors;
    }
}

/**
 * @brief Handle device-management requests: identity discovery and bootloader entry.
 *
 * @param[in] frame Borrowed management frame.
 * @return None (void).
 *
 * @details
 * Accepts only eight-byte extended non-RTR frames. Information requests return the complete
 * silicon ID and application status; broadcast discovery replies are staggered by the shortened
 * node address. Entry-to-bootloader requires an exact match of the full UID before acknowledging
 * and resetting, so a shortened-address collision cannot reset a peer.
 */
static void route_management(const can_frame_t *frame)
{
    if (!frame->extended || frame->rtr || frame->dlc != 8)
    {
        return;
    }
    const uint16_t destination = (uint16_t)frame->id;          /**< Low-word device address. */
    const uint32_t message_class = frame->id & CAN_CLASS_MASK; /**< Protocol message class. */
    /** @brief Management acknowledgement, mode byte zero means application. */
    can_frame_t reply = {.id = CAN_RESPONSE | bare_diag.node,
                         .dlc = 8,
                         .extended = true,
                         .data = {CMD_INFO, frame->data[1], STATUS_OK, 0, PROTOCOL_VERSION}};

    /* Full-UID targeting prevents a shortened-address collision from resetting a peer. */
    if (message_class == CAN_ENTER && destination == bare_diag.node &&
        memcmp(frame->data, (const void *)bare_diag.uid, 8) == 0)
    {
        reply.data[0] = CMD_ENTER;
        reply.data[1] = 0;
        send_response(&reply);
        board_request_bootloader();
    }
    else if (message_class == CAN_REQUEST && frame->data[0] == CMD_INFO &&
             (destination == 0 || destination == bare_diag.node))
    {
        /** @brief Full identity returned before the status reply. */
        can_frame_t identity = {.id = CAN_IDENTITY | bare_diag.node, .dlc = 8, .extended = true};
        memcpy(identity.data, (const void *)bare_diag.uid, sizeof(identity.data));
        if (destination == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(1 + bare_diag.node % CAN_DISCOVERY_WINDOW_MS));
        }
        send_response(&identity);
        send_response(&reply);
    }
}

/**
 * @brief Answer an SDO read request from the object dictionary.
 *
 * @param[in] frame Borrowed SDO request frame.
 * @return None (void).
 *
 * @details
 * Accepts eight-byte read requests addressed to this node or the broadcast address, decodes the
 * little-endian object index, and returns its value as little-endian millivolts in an SDO_TX
 * response. Add a new object by adding an index in ObjDict.h and a case below. Analog objects are
 * read through the ADC HAL; the on-board sense inputs use a 2:1 divider, so the pin voltage is
 * doubled.
 */
static void route_sdo(const can_frame_t *frame)
{
    const uint16_t destination = (uint16_t)frame->id; /**< Low-word device address. */
    if ((destination != 0 && destination != bare_diag.node) || !frame->extended || frame->rtr ||
        frame->dlc != 8)
    {
        return;
    }
    const uint16_t index = (uint16_t)(frame->data[1] | (frame->data[2] << 8)); /**< Object. */
    can_frame_t reply = {.id = CAN_SDO_TX | bare_diag.node, .dlc = 8, .extended = true};
    reply.data[0] = SDO_RESPONSE;
    reply.data[1] = frame->data[1];
    reply.data[2] = frame->data[2];
    reply.data[3] = frame->data[3];

    if (frame->data[0] != SDO_READ)
    {
        reply.data[0] = SDO_ABORT;
    }
    else
    {
        switch (index)
        {
        case OBJ_VUSB_MV:
            put_u32(reply.data + 4, (uint32_t)adc_hal_read_mv(PIN_ADC_VUSB) * 2u);
            break;
        case OBJ_VREF_MV:
            put_u32(reply.data + 4, (uint32_t)adc_hal_read_mv(PIN_ADC_VREF) * 2u);
            break;
        default:
            reply.data[0] = SDO_ABORT;
            break;
        }
    }
    send_response(&reply);
}

/**
 * @brief Forward received process data to the application hook.
 *
 * @param[in] frame Borrowed RPDO frame.
 * @return None (void).
 *
 * @details
 * Passes the frame to the weak application_rpdo_received hook. Process data is not addressed or
 * acknowledged, so the router applies no filtering beyond message class.
 */
static void route_rpdo(const can_frame_t *frame)
{
    application_rpdo_received(frame);
}

/** @brief One entry in the message-class dispatch table. */
typedef struct
{
    uint32_t message_class;                    /**< Frame class after CAN_CLASS_MASK. */
    void (*handler)(const can_frame_t *frame); /**< Handler for a matching class. */
    const char *name;                          /**< Debug label. */
} can_route_t;

/**
 * @brief Message-class dispatch table.
 *
 * Add a row here and a matching handler above to service a new object type. Management shares
 * one handler across its request and enter classes; list each class that should reach it.
 */
static const can_route_t routes[] = {
    {CAN_REQUEST, route_management, "management-request"},
    {CAN_ENTER, route_management, "management-enter"},
    {CAN_SDO_RX, route_sdo, "sdo"},
    {CAN_RPDO, route_rpdo, "rpdo"},
};

void can_router_dispatch(const can_frame_t *frame)
{
    const uint32_t message_class = frame->id & CAN_CLASS_MASK; /**< Class to route on. */
    for (unsigned index = 0; index < sizeof(routes) / sizeof(routes[0]); ++index)
    {
        if (routes[index].message_class == message_class)
        {
            routes[index].handler(frame);
            return;
        }
    }
}

bool can_tpdo_send(const uint8_t *data, uint8_t dlc)
{
    if (dlc > 8 || (data == NULL && dlc != 0))
    {
        return false;
    }
    can_frame_t frame = {.id = CAN_TPDO | bare_diag.node, .dlc = dlc, .extended = true};
    if (dlc != 0)
    {
        memcpy(frame.data, data, dlc);
    }
    const bool sent = board_can_send(&frame); /**< Bounded transmit result. */
    if (sent)
    {
        ++bare_diag.transmitted;
    }
    else
    {
        ++bare_diag.tx_errors;
    }
    return sent;
}

/**
 * @brief Weak default RPDO handler; override in application code.
 *
 * @param[in] frame Borrowed RPDO frame.
 * @return None (void).
 */
__attribute__((weak)) void application_rpdo_received(const can_frame_t *frame)
{
    (void)frame;
}

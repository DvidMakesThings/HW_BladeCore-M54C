/**
 * @file src/Software/BaReTOS/ObjDict.h
 *
 * @version 1.0.0
 * @date 2026-09-20
 *
 * @details
 * Object dictionary for the CAN application: the command bytes and object indices used by the
 * SDO, RPDO, and TPDO handlers in can_router.c. Keep every wire-protocol number here so the
 * firmware and host tools share one list. Add a new object by adding an index define and a
 * matching case in the relevant router handler.
 *
 * @project BaReTOS - BladeCore-M54C firmware
 * @github https://github.com/DvidMakesThings/HW_BladeCore-M54C
 */
#pragma once

/* -------------------------------------------------------------------------- */
/*  SDO - command bytes (data[0]) and object indices (data[1..2], LE)         */
/* -------------------------------------------------------------------------- */
#define SDO_READ 0x40u     /**< Request: read an object. */
#define SDO_RESPONSE 0x43u /**< Response: object value in data[4..7]. */
#define SDO_ABORT 0x80u    /**< Response: unknown object or bad request. */

#define OBJ_VUSB_MV 0x2000u /**< USB VBUS sense voltage, millivolts. */
#define OBJ_VREF_MV 0x2001u /**< 3.00 V reference sense voltage, millivolts. */

/* -------------------------------------------------------------------------- */
/*  RPDO - inbound process-data object indices                                */
/* -------------------------------------------------------------------------- */
/* Add received process-data object indices here. */

/* -------------------------------------------------------------------------- */
/*  TPDO - outbound process-data object indices                               */
/* -------------------------------------------------------------------------- */
/* Add transmitted process-data object indices here. */

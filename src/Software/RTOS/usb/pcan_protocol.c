/*
 * pcan_protocol.c - PCAN-USB command handling and message codec
 */

#include "usb/pcan_protocol.h"
#include "pico/unique_id.h"
#include <string.h>

/* -------------------------------------------------------------------------- */
/*  Internal state                                                             */
/* -------------------------------------------------------------------------- */
static bool s_bus_on;
static bool s_silent;
static uint8_t s_btr0;
static uint8_t s_btr1;
static uint8_t s_device_id;
static uint8_t s_err_mask;

/* -------------------------------------------------------------------------- */
/*  Serial number from flash unique ID                                         */
/* -------------------------------------------------------------------------- */
uint32_t pcan_get_serial(void)
{
    pico_unique_board_id_t uid;
    pico_get_unique_board_id(&uid);
    /* Use first 4 bytes as a 32-bit serial (LE) */
    uint32_t sn = 0;
    sn |= (uint32_t)uid.id[0];
    sn |= (uint32_t)uid.id[1] << 8;
    sn |= (uint32_t)uid.id[2] << 16;
    sn |= (uint32_t)uid.id[3] << 24;
    return sn;
}

/* -------------------------------------------------------------------------- */
/*  BTR -> MCP2515 CNF conversion                                             */
/*                                                                             */
/*  Both SJA1000 and MCP2515 use the same TQ formula with a 16 MHz crystal:   */
/*    TQ = 2 * (BRP_field + 1) / Fosc                                        */
/*  So CNF1 = BTR0 directly.                                                  */
/*  TSEG1 (SJA1000) = PropSeg + PS1 (MCP2515).  We set PropSeg = 1.          */
/*  TSEG2 = PS2.                                                               */
/* -------------------------------------------------------------------------- */
void pcan_get_mcp_timing(uint8_t *cnf1, uint8_t *cnf2, uint8_t *cnf3)
{
    /* CNF1 = BTR0  (BRP and SJW have identical encoding) */
    *cnf1 = s_btr0;

    uint8_t tseg1 = (s_btr1 & 0x0Fu) + 1u;        /* 1..16 */
    uint8_t tseg2 = ((s_btr1 >> 4) & 0x07u) + 1u; /* 1..8 */
    uint8_t sam = (s_btr1 >> 7) & 1u;

    /*
     * Split TSEG1 into PropSeg (1..8) and PS1 (1..8).
     * Both fields are 3-bit (value - 1 stored), so max real value is 8.
     * Strategy: maximize PS1 first, remainder goes to PropSeg.
     */
    uint8_t ps1, propseg;
    if (tseg1 <= 8u)
    {
        propseg = 1u;
        ps1 = (tseg1 > 1u) ? (tseg1 - 1u) : 1u;
    }
    else
    {
        ps1 = 8u;
        propseg = tseg1 - 8u;
        if (propseg > 8u)
            propseg = 8u;
    }

    /* CNF2: BTLMODE=1 | SAM | PS1-1(3b) | PropSeg-1(3b) */
    *cnf2 = 0x80u | ((uint8_t)(sam & 1u) << 6) | ((uint8_t)(ps1 - 1u) << 3) | (propseg - 1u);

    /* CNF3: PS2-1 */
    *cnf3 = (uint8_t)(tseg2 - 1u);
}

bool pcan_is_bus_on(void)
{
    return s_bus_on;
}

/* -------------------------------------------------------------------------- */
/*  Command handler                                                            */
/* -------------------------------------------------------------------------- */
bool pcan_handle_command(const uint8_t *cmd_buf, uint8_t *resp_buf)
{
    uint8_t func = cmd_buf[0];
    uint8_t num = cmd_buf[1];
    const uint8_t *args = &cmd_buf[2];

    memset(resp_buf, 0, PCAN_USB_CMD_LEN);
    resp_buf[0] = func;
    resp_buf[1] = num;

    switch (func)
    {

    /* ---- Serial number ------------------------------------------------- */
    case PCAN_CMD_SN:
        if (num == PCAN_CMD_GET)
        {
            uint32_t sn = pcan_get_serial();
            resp_buf[2] = (uint8_t)(sn);
            resp_buf[3] = (uint8_t)(sn >> 8);
            resp_buf[4] = (uint8_t)(sn >> 16);
            resp_buf[5] = (uint8_t)(sn >> 24);
            return true;
        }
        break;

    /* ---- Device / channel ID ------------------------------------------- */
    case PCAN_CMD_DEVID:
        if (num == PCAN_CMD_GET)
        {
            resp_buf[2] = s_device_id;
            return true;
        }
        if (num == PCAN_CMD_SET)
        {
            s_device_id = args[0];
        }
        break;

    /* ---- Bitrate (BTR0/BTR1) ------------------------------------------ */
    case PCAN_CMD_BITRATE:
        if (num == PCAN_CMD_SET)
        {
            s_btr1 = args[0];
            s_btr0 = args[1];
        }
        break;

    /* ---- Bus on/off and silent mode ------------------------------------ */
    case PCAN_CMD_SET_BUS:
        if (num == PCAN_BUS_XCVER)
        {
            s_bus_on = (args[0] != 0);
        }
        else if (num == PCAN_BUS_SILENT)
        {
            s_silent = (args[0] != 0);
        }
        break;

    /* ---- SJA1000 register (init/normal mode) --------------------------- */
    case PCAN_CMD_REGISTER:
        /* Bus on/off is controlled exclusively by SET_BUS XCVER.
         * Do NOT touch s_bus_on here: the Windows PCAN driver may
         * send SJA1000_MODE_INIT in a different order than Linux,
         * which could race with a SET_BUS ON already in flight. */
        break;

    /* ---- Error frame reporting ----------------------------------------- */
    case PCAN_CMD_ERR_FR:
        if (num == PCAN_CMD_SET)
        {
            s_err_mask = args[0];
        }
        break;

    /* ---- External VCC (not applicable, acknowledge silently) ----------- */
    case PCAN_CMD_EXT_VCC:
        break;

    /* ---- LED control --------------------------------------------------- */
    case PCAN_CMD_LED:
        /* LED is controlled by heartbeat task; ignore host LED cmds */
        break;

    /* ---- BOOTSEL reboot (custom extension, func=0xFF) ------------------ */
    case 0xFFu:
        if (num == 0xFFu &&
            args[0] == 0xB0u && args[1] == 0x07u &&
            args[2] == 0x10u && args[3] == 0xADu)
        {
            /* Reboot into UF2 BOOTSEL mode for picotool flashing */
            extern void pcan_trigger_bootsel(void);
            pcan_trigger_bootsel();
        }
        break;

    default:
        break;
    }

    return false; /* no response needed for SET commands */
}

/* -------------------------------------------------------------------------- */
/*  Decode host TX message -> CAN frames                                       */
/* -------------------------------------------------------------------------- */
int pcan_decode_tx_msg(const uint8_t *buf, uint32_t len,
                       pcan_can_frame_t *frames, int max_frames)
{
    if (len < PCAN_USB_MSG_HEADER_LEN)
    {
        return -1;
    }

    /* buf[0] = message type (expect PCAN_MSG_TX_CAN = 0x02) */
    /* buf[1] = record count */
    uint8_t rec_count = buf[1];
    const uint8_t *ptr = buf + PCAN_USB_MSG_HEADER_LEN;
    const uint8_t *end = buf + len;
    int decoded = 0;

    for (uint8_t i = 0; i < rec_count && decoded < max_frames; i++)
    {
        if (ptr >= end)
            break;

        uint8_t sl = *ptr++;
        uint8_t dlc = sl & PCAN_SL_DLC_MASK;
        if (dlc > 8)
            dlc = 8;

        pcan_can_frame_t *f = &frames[decoded];
        memset(f, 0, sizeof(*f));
        f->dlc = dlc;

        if (sl & PCAN_SL_RTR)
        {
            f->flags |= PCAN_FRAME_FLAG_RTR;
        }

        if (sl & PCAN_SL_EXT_ID)
        {
            /* Extended ID: 4 bytes LE, ID in bits 31..3 */
            f->flags |= PCAN_FRAME_FLAG_EFF;
            if (ptr + 4 > end)
                return decoded;
            uint32_t id_flags = (uint32_t)ptr[0] | ((uint32_t)ptr[1] << 8) | ((uint32_t)ptr[2] << 16) | ((uint32_t)ptr[3] << 24);
            f->id = id_flags >> 3;
            ptr += 4;
        }
        else
        {
            /* Standard ID: 2 bytes LE, ID in bits 15..5 */
            if (ptr + 2 > end)
                return decoded;
            uint16_t id_flags = (uint16_t)ptr[0] | ((uint16_t)ptr[1] << 8);
            f->id = id_flags >> 5;
            ptr += 2;
        }

        /* Data bytes */
        if (!(sl & PCAN_SL_RTR))
        {
            if (ptr + dlc > end)
                return decoded;
            memcpy(f->data, ptr, dlc);
            ptr += dlc;

            /* Skip loopback writer byte if SRR bit set in id_flags */
            /* (we already consumed id_flags, re-check original byte) */
            /* The SRR bit is bit 0 of the raw id_flags word */
            /* For simplicity: if ptr < end and there's an extra byte, skip it
               when loopback is indicated. We don't support loopback, so just
               consume if present. The last byte of the buffer is the packet
               counter -- we handle that outside the loop. */
        }

        decoded++;
    }

    return decoded;
}

/* -------------------------------------------------------------------------- */
/*  Encode CAN RX frames -> PCAN message for host                             */
/* -------------------------------------------------------------------------- */
int pcan_encode_rx_msg(uint8_t *buf, const pcan_can_frame_t *frames,
                       int count, uint16_t timestamp)
{
    if (count <= 0)
        return 0;

    /* Always produce a full 64-byte packet.  The PCAN-USB protocol uses
     * fixed-size 64-byte USB transfers; the Windows PEAK driver may ignore
     * or reject short packets. */
    memset(buf, 0, PCAN_USB_EP_SIZE);

    uint8_t *ptr = buf;
    ptr[0] = 0x02u; /* Message type byte.  The Linux driver ignores byte[0]
                     * entirely.  Tested 0x00-0x07, 0x80, 0x82 on Windows --
                     * none change CAN_Read behavior.  Using 0x02 (same as
                     * the Linux TX encoder uses for PCAN_USB_MSG_TX_CAN). */
    ptr[1] = (uint8_t)count;
    ptr += PCAN_USB_MSG_HEADER_LEN;

    for (int i = 0; i < count; i++)
    {
        const pcan_can_frame_t *f = &frames[i];

        /* Safety: don't overflow 64-byte buffer */
        if ((ptr - buf) + 15 > PCAN_USB_EP_SIZE)
            break;

        /* Status/len byte: DLC + flags.  Do NOT set TIMESTAMP bit (0x80);
         * the real PCAN-USB firmware does not set it for data frames.
         * Timestamps are always present regardless of this bit. */
        uint8_t sl = (f->dlc & PCAN_SL_DLC_MASK);
        if (f->flags & PCAN_FRAME_FLAG_RTR)
            sl |= PCAN_SL_RTR;
        if (f->flags & PCAN_FRAME_FLAG_EFF)
            sl |= PCAN_SL_EXT_ID;
        *ptr++ = sl;

        /* CAN ID */
        if (f->flags & PCAN_FRAME_FLAG_EFF)
        {
            uint32_t id_word = f->id << 3;
            *ptr++ = (uint8_t)(id_word);
            *ptr++ = (uint8_t)(id_word >> 8);
            *ptr++ = (uint8_t)(id_word >> 16);
            *ptr++ = (uint8_t)(id_word >> 24);
        }
        else
        {
            uint16_t id_word = (uint16_t)(f->id << 5);
            *ptr++ = (uint8_t)(id_word);
            *ptr++ = (uint8_t)(id_word >> 8);
        }

        /* Timestamp: first record gets 2 bytes, subsequent get 1 byte */
        if (i == 0)
        {
            *ptr++ = (uint8_t)(timestamp);
            *ptr++ = (uint8_t)(timestamp >> 8);
        }
        else
        {
            *ptr++ = (uint8_t)(timestamp);
        }

        /* Data */
        if (!(f->flags & PCAN_FRAME_FLAG_RTR))
        {
            uint8_t dlc = f->dlc;
            if (dlc > 8)
                dlc = 8;
            memcpy(ptr, f->data, dlc);
            ptr += dlc;
        }
    }

    /* Always return full 64-byte packet size */
    return PCAN_USB_EP_SIZE;
}

/* -------------------------------------------------------------------------- */
/*  Encode timesync event (sent on bus-on to init driver timestamp ref)        */
/* -------------------------------------------------------------------------- */
int pcan_encode_bus_active_msg(uint8_t *buf, uint16_t timestamp)
{
    memset(buf, 0, PCAN_USB_EP_SIZE);

    /* Packet header: type=0x02, count=2 records */
    buf[0] = 0x02u;
    buf[1] = 0x02u;

    /* Record 1: REC_TS (timestamp sync)
     * sl = INTERNAL | 2 (DLC=2 for the timestamp payload) */
    buf[2] = PCAN_SL_INTERNAL | 0x02u;
    buf[3] = PCAN_USB_REC_TS; /* func = 4 */
    buf[4] = 0x01u;           /* num  = 1 */
    buf[5] = (uint8_t)(timestamp);
    buf[6] = (uint8_t)(timestamp >> 8);

    /* Record 2: REC_ERROR (bus state = ERROR_ACTIVE)
     * sl = INTERNAL only (no TIMESTAMP, DLC=0) */
    buf[7] = PCAN_SL_INTERNAL;
    buf[8] = PCAN_USB_REC_ERROR; /* func = 1 */
    buf[9] = 0x00u;              /* n = no errors */

    return PCAN_USB_EP_SIZE;
}

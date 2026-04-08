/*
 * pcan_usb_class.c - TinyUSB custom class driver for PCAN-USB protocol
 *
 * Implements a vendor-specific USB interface with 4 bulk endpoints that
 * matches the PCAN-USB adapter layout:
 *   EP1 OUT (0x01) - Command out
 *   EP1 IN  (0x81) - Command in (response)
 *   EP2 OUT (0x02) - CAN message out (host TX)
 *   EP2 IN  (0x82) - CAN message in  (host RX)
 */

#include "tusb.h"
#include "device/usbd.h"
#include "device/usbd_pvt.h"
#include "usb/pcan_protocol.h"
#include "hal/can_bridge.h"
#include "pico/bootrom.h"
#include <string.h>

/* -------------------------------------------------------------------------- */
/*  State                                                                      */
/* -------------------------------------------------------------------------- */
static uint8_t s_ep_cmd_out;
static uint8_t s_ep_cmd_in;
static uint8_t s_ep_msg_out;
static uint8_t s_ep_msg_in;

static uint8_t s_cmd_out_buf[PCAN_USB_EP_SIZE];
static uint8_t s_cmd_in_buf[PCAN_USB_EP_SIZE];
static uint8_t s_msg_out_buf[PCAN_USB_EP_SIZE];

/* RX message buffer -- filled by can_bridge and submitted here */
static uint8_t s_msg_in_buf[PCAN_USB_EP_SIZE];
static volatile bool s_msg_in_busy;

/* -------------------------------------------------------------------------- */
/*  Public: submit a CAN RX message to the host                                */
/* -------------------------------------------------------------------------- */
bool pcan_usb_send_rx_msg(const uint8_t *buf, uint16_t len)
{
    if (s_msg_in_busy || !tud_ready())
        return false;
    if (len > PCAN_USB_EP_SIZE)
        len = PCAN_USB_EP_SIZE;

    memcpy(s_msg_in_buf, buf, len);
    s_msg_in_busy = true;
    if (!usbd_edpt_xfer(0, s_ep_msg_in, s_msg_in_buf, len))
    {
        s_msg_in_busy = false;
        return false;
    }
    return true;
}

bool pcan_usb_is_rx_busy(void)
{
    return s_msg_in_busy;
}

/* -------------------------------------------------------------------------- */
/*  Class driver callbacks                                                     */
/* -------------------------------------------------------------------------- */
static void pcan_init(void)
{
    s_ep_cmd_out = 0;
    s_ep_cmd_in = 0;
    s_ep_msg_out = 0;
    s_ep_msg_in = 0;
    s_msg_in_busy = false;
}

static void pcan_reset(uint8_t rhport)
{
    (void)rhport;
    s_msg_in_busy = false;
}

static uint16_t pcan_open(uint8_t rhport, tusb_desc_interface_t const *desc_itf,
                          uint16_t max_len)
{
    (void)rhport;

    /* We only claim vendor-class interfaces */
    if (desc_itf->bInterfaceClass != TUSB_CLASS_VENDOR_SPECIFIC)
        return 0;

    uint16_t drv_len = sizeof(tusb_desc_interface_t);
    uint8_t const *p_desc = (uint8_t const *)desc_itf + drv_len;

    /* Open all 4 bulk endpoints */
    for (int i = 0; i < 4 && drv_len < max_len; i++)
    {
        tusb_desc_endpoint_t const *ep = (tusb_desc_endpoint_t const *)p_desc;
        if (ep->bDescriptorType != TUSB_DESC_ENDPOINT)
            break;

        usbd_edpt_open(rhport, ep);

        uint8_t addr = ep->bEndpointAddress;
        if (addr == PCAN_EP_CMD_OUT)
            s_ep_cmd_out = addr;
        else if (addr == PCAN_EP_CMD_IN)
            s_ep_cmd_in = addr;
        else if (addr == PCAN_EP_MSG_OUT)
            s_ep_msg_out = addr;
        else if (addr == PCAN_EP_MSG_IN)
            s_ep_msg_in = addr;

        drv_len += tu_desc_len(p_desc);
        p_desc += tu_desc_len(p_desc);
    }

    /* Start receiving on both OUT endpoints */
    if (s_ep_cmd_out)
    {
        usbd_edpt_xfer(rhport, s_ep_cmd_out, s_cmd_out_buf, PCAN_USB_EP_SIZE);
    }
    if (s_ep_msg_out)
    {
        usbd_edpt_xfer(rhport, s_ep_msg_out, s_msg_out_buf, PCAN_USB_EP_SIZE);
    }

    return drv_len;
}

static bool pcan_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                 tusb_control_request_t const *request)
{
    (void)rhport;

    /* Handle bootloader trigger via vendor control request */
    if (stage == CONTROL_STAGE_SETUP &&
        request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR &&
        request->bmRequestType_bit.direction == TUSB_DIR_OUT &&
        request->bRequest == 0xDAu &&
        request->wValue == 0xB007u &&
        request->wIndex == 0x10ADu)
    {
        /* Trigger bootloader on next reset */
        extern volatile uint32_t bootloader_trigger;
        bootloader_trigger = 0xB00710ADu;
        tud_control_status(rhport, request);

        /* Schedule watchdog reboot after USB status stage completes */
        extern void bootloader_schedule_reboot(void);
        bootloader_schedule_reboot();
        return true;
    }

    return false;
}

static bool pcan_xfer_cb(uint8_t rhport, uint8_t ep_addr,
                         xfer_result_t result, uint32_t xferred_bytes)
{
    (void)rhport;

    if (result != XFER_RESULT_SUCCESS)
    {
        /* Re-submit on failure */
        if (ep_addr == s_ep_cmd_out)
        {
            usbd_edpt_xfer(rhport, s_ep_cmd_out, s_cmd_out_buf, PCAN_USB_EP_SIZE);
        }
        else if (ep_addr == s_ep_msg_out)
        {
            usbd_edpt_xfer(rhport, s_ep_msg_out, s_msg_out_buf, PCAN_USB_EP_SIZE);
        }
        else if (ep_addr == s_ep_msg_in)
        {
            s_msg_in_busy = false;
        }
        return true;
    }

    /* ---- Command OUT (host -> device) ---------------------------------- */
    if (ep_addr == s_ep_cmd_out)
    {
        if (xferred_bytes >= PCAN_USB_CMD_LEN)
        {
            bool need_response = pcan_handle_command(s_cmd_out_buf, s_cmd_in_buf);
            if (need_response && s_ep_cmd_in)
            {
                usbd_edpt_xfer(rhport, s_ep_cmd_in, s_cmd_in_buf, PCAN_USB_CMD_LEN);
            }
        }
        /* Re-submit OUT for next command */
        usbd_edpt_xfer(rhport, s_ep_cmd_out, s_cmd_out_buf, PCAN_USB_EP_SIZE);
        return true;
    }

    /* ---- Command IN (device -> host response) -------------------------- */
    if (ep_addr == s_ep_cmd_in)
    {
        /* Response sent; nothing more to do */
        return true;
    }

    /* ---- Message OUT (host TX -> CAN bus) ------------------------------ */
    if (ep_addr == s_ep_msg_out)
    {
        if (xferred_bytes >= PCAN_USB_MSG_HEADER_LEN)
        {
            pcan_can_frame_t frames[4];
            int n = pcan_decode_tx_msg(s_msg_out_buf, xferred_bytes,
                                       frames, 4);
            for (int i = 0; i < n; i++)
            {
                can_bridge_enqueue_tx(&frames[i]);
            }
        }
        /* Re-submit OUT for next message */
        usbd_edpt_xfer(rhport, s_ep_msg_out, s_msg_out_buf, PCAN_USB_EP_SIZE);
        return true;
    }

    /* ---- Message IN (CAN bus RX -> host) ------------------------------- */
    if (ep_addr == s_ep_msg_in)
    {
        s_msg_in_busy = false;
        return true;
    }

    return true;
}

/* -------------------------------------------------------------------------- */
/*  BOOTSEL reboot trigger (from PCAN command context)                         */
/* -------------------------------------------------------------------------- */
void pcan_trigger_bootsel(void)
{
    reset_usb_boot(0, 0);
}

/* -------------------------------------------------------------------------- */
/*  Register with TinyUSB as a custom class driver                             */
/* -------------------------------------------------------------------------- */
static const usbd_class_driver_t s_pcan_driver = {
#if CFG_TUSB_DEBUG >= 2
    .name = "PCAN",
#endif
    .init = pcan_init,
    .reset = pcan_reset,
    .open = pcan_open,
    .control_xfer_cb = pcan_control_xfer_cb,
    .xfer_cb = pcan_xfer_cb,
    .sof = NULL,
};

usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count)
{
    *driver_count = 1;
    return &s_pcan_driver;
}

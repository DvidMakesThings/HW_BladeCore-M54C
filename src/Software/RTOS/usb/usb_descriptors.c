/*
 * usb_descriptors.c - PCAN-USB compatible USB descriptors for BladeCore-M54C
 *
 * NOTE: Uses PEAK Systems VID/PID for driver compatibility.
 *       For development and testing purposes only.
 */

#include "tusb.h"
#include "usb/pcan_protocol.h"
#include "pico/unique_id.h"
#include <string.h>

/* -------------------------------------------------------------------------- */
/*  Endpoint layout                                                            */
/* -------------------------------------------------------------------------- */
/*  EP1 OUT (0x01) - Command OUT    64 bytes bulk                             */
/*  EP1 IN  (0x81) - Command IN     64 bytes bulk                             */
/*  EP2 OUT (0x02) - Message OUT    64 bytes bulk                             */
/*  EP2 IN  (0x82) - Message IN     64 bytes bulk                             */

/* -------------------------------------------------------------------------- */
/*  Device descriptor                                                          */
/* -------------------------------------------------------------------------- */
static const tusb_desc_device_t s_desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200u,
    .bDeviceClass = 0x00u, /* class defined at interface level */
    .bDeviceSubClass = 0x00u,
    .bDeviceProtocol = 0x00u,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = PCAN_USB_VENDOR_ID,
    .idProduct = PCAN_USB_PRODUCT_ID,
    .bcdDevice = 0x001Cu, /* fw rev 28 -- safe classic PCAN-USB revision */
    .iManufacturer = 1u,
    .iProduct = 2u,
    .iSerialNumber = 3u,
    .bNumConfigurations = 1u,
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&s_desc_device;
}

/* -------------------------------------------------------------------------- */
/*  Configuration descriptor                                                   */
/*  1 interface, 4 bulk endpoints                                              */
/* -------------------------------------------------------------------------- */
#define CONFIG_TOTAL_LEN (9 + 9 + 4 * 7) /* config(9) + interface(9) + 4*endpoint(7) = 46 */

static const uint8_t s_desc_config[] = {
    /* Configuration descriptor */
    9,                              /* bLength */
    TUSB_DESC_CONFIGURATION,        /* bDescriptorType */
    CONFIG_TOTAL_LEN & 0xFF,        /* wTotalLength (lo) */
    (CONFIG_TOTAL_LEN >> 8) & 0xFF, /* wTotalLength (hi) */
    1,                              /* bNumInterfaces */
    1,                              /* bConfigurationValue */
    0,                              /* iConfiguration */
    0x80u,                          /* bmAttributes: bus-powered */
    250,                            /* bMaxPower: 500 mA */

    /* Interface descriptor */
    9,                          /* bLength */
    TUSB_DESC_INTERFACE,        /* bDescriptorType */
    0,                          /* bInterfaceNumber */
    0,                          /* bAlternateSetting */
    4,                          /* bNumEndpoints */
    TUSB_CLASS_VENDOR_SPECIFIC, /* bInterfaceClass */
    0x00u,                      /* bInterfaceSubClass */
    0x00u,                      /* bInterfaceProtocol */
    0,                          /* iInterface */

    /* EP1 OUT - Command OUT */
    7,                  /* bLength */
    TUSB_DESC_ENDPOINT, /* bDescriptorType */
    PCAN_EP_CMD_OUT,    /* bEndpointAddress: 0x01 */
    TUSB_XFER_BULK,     /* bmAttributes: bulk */
    64,
    0, /* wMaxPacketSize: 64 */
    0, /* bInterval */

    /* EP1 IN - Command IN */
    7,
    TUSB_DESC_ENDPOINT,
    PCAN_EP_CMD_IN, /* bEndpointAddress: 0x81 */
    TUSB_XFER_BULK,
    64,
    0,
    0,

    /* EP2 OUT - Message OUT */
    7,
    TUSB_DESC_ENDPOINT,
    PCAN_EP_MSG_OUT, /* bEndpointAddress: 0x02 */
    TUSB_XFER_BULK,
    64,
    0,
    0,

    /* EP2 IN - Message IN */
    7,
    TUSB_DESC_ENDPOINT,
    PCAN_EP_MSG_IN, /* bEndpointAddress: 0x82 */
    TUSB_XFER_BULK,
    64,
    0,
    0,
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return s_desc_config;
}

/* -------------------------------------------------------------------------- */
/*  String descriptors                                                         */
/* -------------------------------------------------------------------------- */
static char s_serial_str[17] = {0};

static void build_serial_str(void)
{
    pico_unique_board_id_t uid;
    pico_get_unique_board_id(&uid);
    const char *hex = "0123456789ABCDEF";
    for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++)
    {
        s_serial_str[i * 2] = hex[uid.id[i] >> 4];
        s_serial_str[i * 2 + 1] = hex[uid.id[i] & 0x0Fu];
    }
    s_serial_str[16] = '\0';
}

static const char *const s_string_desc[] = {
    (const char[]){0x09u, 0x04u}, /* 0: Language ID (English) */
    "DvidMakesThings",            /* 1: Manufacturer */
    "BladeCore-M54C CAN",         /* 2: Product */
    s_serial_str,                 /* 3: Serial number */
};

static uint16_t s_desc_str[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;

    if (s_serial_str[0] == '\0')
        build_serial_str();

    uint8_t chr_count;

    if (index == 0u)
    {
        memcpy(&s_desc_str[1], s_string_desc[0], 2u);
        chr_count = 1u;
    }
    else
    {
        if (index >= (uint8_t)(sizeof(s_string_desc) / sizeof(s_string_desc[0])))
        {
            return NULL;
        }
        const char *str = s_string_desc[index];
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31u)
            chr_count = 31u;
        for (uint8_t i = 0u; i < chr_count; i++)
        {
            s_desc_str[1u + i] = (uint16_t)str[i];
        }
    }

    s_desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8u) | (2u * chr_count + 2u));
    return s_desc_str;
}

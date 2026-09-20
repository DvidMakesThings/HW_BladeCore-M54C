/**
 * @file src/Software/CAN_Bootloader/main.c
 *
 * @version 1.0.0
 * @date 2026-09-18
 *
 * @details
 * Starts the standalone CAN bootloader through the Pico SDK copy-to-RAM startup. A valid
 * application is checked and entered through its fixed-address reset vector unless
 * watchdog scratch explicitly requests recovery. The recovery loop selects devices by complete
 * silicon ID, receives sequential CAN updates, verifies flash readback, and commits a validity
 * manifest last. Incomplete or failed transfers remain recoverable without executing partial
 * firmware.
 *
 * @project CAN_Bootloader - BladeCore-M54C firmware
 * @github https://github.com/DvidMakesThings/HW_BladeCore-M54C
 */
#include "CONFIG.h"
#include "board_can.h"
#include "update.h"
#include "pico/stdlib.h"
#include "hardware/structs/scb.h"
#include "hardware/structs/systick.h"
#include "hardware/structs/nvic.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include <string.h>

/** @brief State made visible to SWD without a USB or UART dependency. */
typedef struct
{
    uint32_t node;           /**< Silicon-derived CAN address. */
    uint32_t received;       /**< Addressed protocol frames received. */
    uint32_t status;         /**< Most recent operation result. */
    uint32_t bytes;          /**< Accepted image bytes. */
    uint32_t can_ready;      /**< MCP2515 successfully entered normal mode. */
    uint32_t handover;       /**< A verified application was selected for handover. */
    uint32_t running_in_ram; /**< Main code address lies in SRAM. */
    uint8_t uid[8];          /**< Full silicon identity. */
} boot_diagnostics_t;

volatile boot_diagnostics_t boot_diag; /**< Debugger-readable runtime state. */
static update_t session;               /**< Current firmware transfer, owned by the main loop. */
static uint8_t identity[8];            /**< Full identity used to authorise addressed updates. */
static uint16_t node;                  /**< Stable shortened CAN address. */

/**
 * @brief Enter a verified application's reset handler with a clean main stack.
 *
 * @param[in] stack Initial MSP read from the validated application vector table.
 * @param[in] reset Thumb reset-handler address read from the validated vector table.
 * @return None (void); execution continues in the application's startup code.
 *
 * @details
 * Runs the final handover entirely in assembly because changing MSP invalidates the caller's
 * C stack frame. Clears inherited priority masks, stack limits, and thread control before
 * loading MSP. Interrupt sources have already been disabled and their pending bits cleared.
 * The application enters in privileged secure Thread mode and performs its normal SDK startup.
 */
__attribute__((naked, noreturn)) static void enter_application(uint32_t stack
                                                               __attribute__((unused)),
                                                               uint32_t reset
                                                               __attribute__((unused)))
{
    /* A naked function must contain only basic assembly: r0/r1 hold the two arguments. */
    __asm volatile("movs r2, #0\n"
                   "msr basepri, r2\n"
                   "msr faultmask, r2\n"
                   "msr control, r2\n"
                   "msr msplim, r2\n"
                   "msr psplim, r2\n"
                   "isb\n"
                   "msr msp, r0\n"
                   "cpsie i\n"
                   "bx r1\n");
}

/**
 * @brief Hand over to a fully verified application at its fixed XIP address.
 *
 * @par Parameters
 * None (void).
 * @return None (void); returns only when no valid application is installed.
 *
 * @details
 * Verifies the committed manifest, complete image CRC, and vector bounds before changing
 * processor state. Disables SysTick and every external interrupt, clears pending exceptions,
 * and installs the application's vector table. XIP address translation remains unchanged:
 * application code is linked to APP_BASE, matching its physical flash storage address.
 * Core 1 remains in the ROM wait loop. SDK startup reinitialises peripherals and C data.
 */
static void try_application(void)
{
    if (!application_valid())
    {
        return;
    }
    const uint32_t *vectors = (const uint32_t *)APP_BASE; /**< Validated initial MSP and PC. */
    boot_diag.handover = 1;
    save_and_disable_interrupts();
    systick_hw->csr = 0;
    for (unsigned bank = 0; bank < 2; ++bank) /**< Two banks cover all RP2354 external IRQs. */
    {
        nvic_hw->icer[bank] = 0xffffffffu;
        nvic_hw->icpr[bank] = 0xffffffffu;
    }
    /* Remove SDK alarm and system-exception state before replacing the vector table. */
    scb_hw->icsr = M33_ICSR_PENDSVCLR_BITS | M33_ICSR_PENDSTCLR_BITS;
    scb_hw->vtor = APP_BASE;
    __dsb();
    __isb();
    enter_application(vectors[0], vectors[1]);
}

/**
 * @brief Send the full silicon ID followed by a transaction-matched status.
 *
 * @param[in] transaction Host transaction byte.
 * @param[in] broadcast True requests a deterministic discovery response delay.
 * @return None (void).
 *
 * @details
 * Builds an eight-byte full-UID response followed by a transaction-tagged bootloader status
 * frame. Status reports the protocol version and whether an installed image currently passes
 * complete verification. Broadcast replies use a deterministic address-derived delay to reduce
 * discovery contention; addressed replies are sent immediately.
 */
static void send_information(uint8_t transaction, bool broadcast)
{
    /** @brief Eight-byte silicon identity response. */
    can_frame_t id_frame = {.id = CAN_IDENTITY | node, .dlc = 8, .extended = true};
    /** @brief Mode one identifies the resident bootloader. */
    can_frame_t reply = {.id = CAN_RESPONSE | node,
                         .dlc = 8,
                         .extended = true,
                         .data = {CMD_INFO, transaction, STATUS_OK, 1, PROTOCOL_VERSION}};
    memcpy(id_frame.data, identity, sizeof(identity));
    reply.data[5] = application_valid();
    if (broadcast)
    {
        sleep_ms(1 + node % CAN_DISCOVERY_WINDOW_MS);
    }
    board_can_send(&id_frame);
    board_can_send(&reply);
}

/**
 * @brief Verify the installed image or serve firmware updates from SRAM.
 *
 * @par Parameters
 * None (void).
 * @return Does not return; remains in recovery or resets into the application.
 *
 * @details
 * Consumes a complementary watchdog scratch request and otherwise attempts to boot a fully
 * verified application. Recovery initialises CAN, enforces full-UID selection, and handles
 * bounded, acknowledged transfers with duplicate-control replay. Inactivity deselects the
 * device and abandons an incomplete transfer. A successful boot command resets the MCU so the
 * normal validated vector handover path is used.
 */
int main(void)
{
    const bool requested =
        watchdog_hw->scratch[0] == BOOT_REQUEST_MAGIC &&
        watchdog_hw->scratch[1] == ~BOOT_REQUEST_MAGIC; /**< Explicit recovery request. */
    boot_diag.running_in_ram = (uintptr_t)&main >= 0x20000000u && (uintptr_t)&main < 0x20082000u;
    watchdog_disable();
    watchdog_hw->scratch[0] = 0;
    watchdog_hw->scratch[1] = 0;
    if (!requested)
    {
        try_application();
    }

    node = board_identity(identity);
    boot_diag.node = node;
    memcpy((void *)boot_diag.uid, identity, sizeof(identity));
    gpio_init(PIN_HEARTBEAT);
    gpio_set_dir(PIN_HEARTBEAT, GPIO_OUT);
    while (!(boot_diag.can_ready = board_can_init(false)))
    {
        gpio_xor_mask64(1ull << PIN_HEARTBEAT);
        sleep_ms(BOOT_HEARTBEAT_PERIOD_MS);
    }

    bool selected = false; /**< Full-UID selection is required before destructive commands. */
    bool cached = false;   /**< A previous control reply is available for retransmission. */
    uint8_t previous_request[8] = {0}; /**< Exact previous control payload. */
    can_frame_t previous_reply = {0};  /**< Reply cached to avoid repeating flash operations. */
    absolute_time_t blink = make_timeout_time_ms(BOOT_HEARTBEAT_PERIOD_MS); /**< LED deadline. */
    absolute_time_t last_activity = get_absolute_time(); /**< Last selected-host activity. */

    for (;;)
    {
        if (time_reached(blink))
        {
            gpio_xor_mask64(1ull << PIN_HEARTBEAT);
            blink = make_timeout_time_ms(BOOT_HEARTBEAT_PERIOD_MS);
        }
        if (selected && absolute_time_diff_us(last_activity, get_absolute_time()) >
                            (int64_t)BOOT_SESSION_TIMEOUT_MS * 1000)
        {
            /* Never boot automatically after an incomplete or abandoned transfer. */
            session.active = false;
            selected = false;
            cached = false;
        }

        can_frame_t frame; /**< Next CAN frame to inspect. */
        if (!board_can_receive(&frame))
        {
            tight_loop_contents();
            continue;
        }
        if (!frame.extended || frame.rtr || frame.dlc != 8)
        {
            continue;
        }
        const uint32_t message_class = frame.id & CAN_CLASS_MASK; /**< Protocol class. */
        const uint16_t destination = (uint16_t)frame.id;          /**< Address from identifier. */
        if (destination != node &&
            !(destination == 0 && message_class == CAN_REQUEST && frame.data[0] == CMD_INFO))
        {
            continue;
        }
        if (message_class == CAN_REQUEST && frame.data[0] == CMD_INFO)
        {
            send_information(frame.data[1], destination == 0);
            continue;
        }

        /** @brief Default successful reply from recovery mode. */

        can_frame_t reply = {
            .id = CAN_RESPONSE | node, .dlc = 8, .extended = true, .data = {0, 0, STATUS_OK, 1}};
        if (message_class == CAN_SELECT || message_class == CAN_ENTER)
        {
            if (memcmp(frame.data, identity, sizeof(identity)) != 0)
            {
                /* A device with the same shortened address must stay silent. */
                selected = false;
                session.active = false;
                cached = false;
                continue;
            }
            selected = true;
            last_activity = get_absolute_time();
            reply.data[0] = message_class == CAN_SELECT ? CMD_SELECT : CMD_ENTER;
            board_can_send(&reply);
            continue;
        }
        if (!selected || (message_class != CAN_REQUEST && message_class != CAN_DATA))
        {
            continue;
        }
        ++boot_diag.received;
        last_activity = get_absolute_time();

        if (message_class == CAN_DATA)
        {
            const uint32_t offset = get_u32(frame.data); /**< Image byte offset. */
            reply.data[0] = CMD_DATA;
            reply.data[2] = update_data(&session, offset, frame.data + 4);
            put_u32(reply.data + 4, offset);
            cached = false;
        }
        else
        {
            if (cached && memcmp(previous_request, frame.data, sizeof(previous_request)) == 0)
            {
                board_can_send(&previous_reply);
                continue;
            }
            reply.data[0] = frame.data[0];
            reply.data[1] = frame.data[1];
            switch (frame.data[0])
            {
            case CMD_BEGIN:
                reply.data[2] = update_begin(&session, get_u32(frame.data + 2));
                break;
            case CMD_FINISH:
                reply.data[2] = update_finish(&session, get_u32(frame.data + 2));
                break;
            case CMD_ABORT:
                session.active = false;
                break;
            case CMD_BOOT:
                if (session.active || !application_valid())
                {
                    reply.data[2] = STATUS_IMAGE;
                }
                break;
            default:
                reply.data[2] = STATUS_ARGUMENT;
                break;
            }
            memcpy(previous_request, frame.data, sizeof(previous_request));
            previous_reply = reply;
            cached = true;
        }
        boot_diag.bytes = session.received;
        boot_diag.status = reply.data[2];
        board_can_send(&reply);
        if (message_class == CAN_REQUEST && frame.data[0] == CMD_BOOT && reply.data[2] == STATUS_OK)
        {
            watchdog_reboot(0, 0, 10);
            while (true)
            {
                tight_loop_contents();
            }
        }
        if (board_can_errors() & 0xc0)
        {
            board_can_clear_overflow();
        }
    }
}

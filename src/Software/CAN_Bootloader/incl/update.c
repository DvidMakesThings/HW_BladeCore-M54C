/**
 * @file src/Software/CAN_Bootloader/incl/update.c
 *
 * @version 1.0.0
 * @date 2026-09-18
 *
 * @details
 * Implements the firmware transfer transaction independently of CAN and the MCU SDK. It
 * rejects invalid sizes and offsets, treats an identical last-frame retry idempotently, and
 * writes a final partial page with erased-value padding. Every page is verified after
 * programming. A complete image CRC and vector check precede the manifest commit, so an
 * interrupted transfer cannot be selected for boot.
 *
 * @project CAN_Bootloader - BladeCore-M54C firmware
 * @github https://github.com/DvidMakesThings/HW_BladeCore-M54C
 */
#include "update.h"
#include <string.h>

/**
 * @brief Validate the persisted manifest, vectors, and complete application CRC.
 *
 * @par Parameters
 * None (void).
 * @return True only for a completely committed and intact application; false otherwise.
 *
 * @details
 * Copies the manifest into an aligned local object and validates its signature, board marker,
 * version, length, and header checksum. Only then does it inspect application vectors and
 * calculate the complete image CRC. Any missing, partially programmed, or corrupted record
 * keeps the board in recovery instead of jumping into unverified code.
 */
bool application_valid(void)
{
    image_manifest_t manifest; /**< Aligned snapshot of the persisted validity record. */
    memcpy(&manifest, storage_read(META_OFFSET), sizeof(manifest));
    /* Short-circuit validation prevents an untrusted length from indexing flash. */
    return manifest_valid(&manifest) &&
           image_vectors_valid(storage_read(APP_OFFSET), manifest.length) &&
           crc32(storage_read(APP_OFFSET), manifest.length) == manifest.crc;
}

/**
 * @brief Invalidate any installed application and begin a sequential transfer.
 *
 * @param[out] state Transfer state to reset.
 * @param[in] length Exact firmware byte count.
 * @return Protocol status; invalid lengths leave the current application untouched.
 *
 * @details
 * Rejects lengths outside the configured application partition without touching flash. For an
 * accepted length, it clears transfer state and erases the dedicated manifest sector before
 * any application data can change. This invalidation order guarantees that a reset or power
 * loss during the update will return to recovery. Application sectors are erased lazily as
 * their first pages arrive.
 */
uint8_t update_begin(update_t *state, uint32_t length)
{
    if (length < 256 || length > APP_MAX_SIZE)
    {
        return STATUS_ARGUMENT;
    }
    memset(state, 0, sizeof(*state));
    /* Invalidate before modifying the application: power loss then stays in recovery. */
    if (!storage_erase(META_OFFSET, 4096))
    {
        return STATUS_FLASH;
    }
    state->length = length;
    state->previous_offset = UINT32_MAX;
    memset(state->page, 0xff, sizeof(state->page));
    state->active = true;
    return STATUS_OK;
}

/**
 * @brief Accept the next four firmware bytes or an identical last-frame retry.
 *
 * @param[in,out] state Active transfer state.
 * @param[in] offset Zero-based firmware byte offset, aligned to four bytes.
 * @param[in] data Four data bytes; bytes past the declared length are ignored.
 * @return Protocol status, including sequence and storage errors.
 *
 * @details
 * Accepts only the next aligned offset in an active transfer, with an exception for an
 * identical retransmission of the last accepted frame. Four-byte payloads accumulate in an
 * SRAM page buffer; a complete page or final partial page is programmed with erased-value
 * padding. Each sector is erased before its first page, and each page is read back before
 * acknowledgement. Storage errors deactivate the transfer without committing a manifest.
 */
uint8_t update_data(update_t *state, uint32_t offset, const uint8_t data[4])
{
    if (!state->active)
    {
        return STATUS_STATE;
    }
    if (offset >= state->length || (offset & 3))
    {
        return STATUS_SEQUENCE;
    }
    /* A lost acknowledgement must not program a page twice or advance the stream. */
    if (offset == state->previous_offset)
    {
        return memcmp(data, state->previous_data, 4) == 0 ? STATUS_OK : STATUS_SEQUENCE;
    }
    if (offset != state->received)
    {
        return STATUS_SEQUENCE;
    }
    uint32_t count = state->length - offset; /**< Bytes belonging to the declared image. */
    if (count > 4)
    {
        count = 4;
    }
    memcpy(state->page + (offset & 255), data, count);
    const uint32_t next = offset + count; /**< Next required byte offset. */

    if (!(next & 255) || next == state->length)
    {
        const uint32_t flash_offset = APP_OFFSET + (offset & ~255u); /**< Completed page address. */
        /* Erase each sector immediately before its first page, never while host data streams. */
        if (!(flash_offset & 4095) && !storage_erase(flash_offset, 4096))
        {
            state->active = false;
            return STATUS_FLASH;
        }
        if (!storage_program(flash_offset, state->page, sizeof(state->page)) ||
            memcmp(storage_read(flash_offset), state->page, sizeof(state->page)) != 0)
        {
            state->active = false;
            return STATUS_FLASH;
        }
        memset(state->page, 0xff, sizeof(state->page));
    }
    state->received = next;
    state->previous_offset = offset;
    memcpy(state->previous_data, data, sizeof(state->previous_data));
    return STATUS_OK;
}

/**
 * @brief Verify the full image and atomically commit its validity record.
 *
 * @param[in,out] state Transfer with all declared bytes received.
 * @param[in] expected_crc Host-calculated IEEE CRC-32 of the exact image length.
 * @return Protocol status; failed verification never commits an image.
 *
 * @details
 * Requires all declared image bytes to have arrived, then recalculates the image CRC directly
 * from flash and validates its initial vectors. Only a fully verified image receives a new
 * manifest containing a checksum of its own header. The final manifest page is the commit
 * operation. Any interrupted, failed, or corrupted commit fails validation on the next boot
 * and leaves the loader available for another update.
 */
uint8_t update_finish(update_t *state, uint32_t expected_crc)
{
    if (!state->active || state->received != state->length)
    {
        return STATUS_STATE;
    }
    if (crc32(storage_read(APP_OFFSET), state->length) != expected_crc)
    {
        return STATUS_CRC;
    }
    if (!image_vectors_valid(storage_read(APP_OFFSET), state->length))
    {
        return STATUS_IMAGE;
    }

    /** @brief Validity record built only after all image checks pass. */

    image_manifest_t manifest = {META_MAGIC,    PROTOCOL_VERSION, BOARD_TYPE,
                                 state->length, expected_crc,     0};
    uint8_t page[256]; /**< SRAM page containing the manifest and erased padding. */
    manifest.header_crc = crc32(&manifest, offsetof(image_manifest_t, header_crc));
    memset(page, 0xff, sizeof(page));
    memcpy(page, &manifest, sizeof(manifest));
    state->active = false;

    /* The final flash write makes the image bootable; any torn write fails header validation. */
    if (!storage_program(META_OFFSET, page, sizeof(page)) || !application_valid())
    {
        return STATUS_FLASH;
    }
    return STATUS_OK;
}

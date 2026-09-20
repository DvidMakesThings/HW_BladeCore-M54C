/**
 * @file src/Software/CAN_Bootloader/incl/update.h
 *
 * @version 1.0.0
 * @date 2026-09-18
 *
 * @details
 * Declares the portable sequential update state and the small flash-storage interface used by
 * the resident bootloader. A transfer invalidates the old manifest first, programs and
 * verifies pages, and commits validity only after complete-image verification. The storage
 * interface is replaced by simulated NOR flash in the native tests, exercising the same
 * production state machine.
 *
 * @project CAN_Bootloader - BladeCore-M54C firmware
 * @github https://github.com/DvidMakesThings/HW_BladeCore-M54C
 */
#pragma once
#include "protocol.h"

/** @brief One sequential firmware transfer with idempotent last-frame retry. */
typedef struct
{
    bool active;              /**< True until abort, error, timeout, or successful finish. */
    uint32_t length;          /**< Declared image length before page padding. */
    uint32_t received;        /**< Number of accepted image bytes. */
    uint32_t previous_offset; /**< Last acknowledged offset, or UINT32_MAX initially. */
    uint8_t page[256];        /**< Aligned-size staging buffer for one flash page. */
    uint8_t previous_data[4]; /**< Last data payload for duplicate consistency checks. */
} update_t;

/**
 * @brief Erase a permitted flash range and verify every byte is erased.
 *
 * @param[in] offset Flash offset, aligned to a 4096-byte sector.
 * @param[in] size Nonzero sector-multiple byte count.
 * @return True on successful erase and readback; bootloader ranges are rejected; false otherwise.
 *
 * @details
 * Rejects unaligned, empty, overflowing, or bootloader-overlapping requests before calling the
 * Pico SDK flash erase routine. Interrupts are disabled during the XIP-off interval, and core
 * 1 is never started by this loader. After XIP is restored, every erased byte is checked for
 * 0xFF before the caller may acknowledge the operation.
 */
bool storage_erase(uint32_t offset, uint32_t size);

/**
 * @brief Program a permitted flash range and verify its complete contents.
 *
 * @param[in] offset Flash offset, aligned to a 256-byte page.
 * @param[in] data SRAM source buffer.
 * @param[in] size Nonzero page-multiple byte count.
 * @return True on successful program and readback; bootloader ranges are rejected; false otherwise.
 *
 * @details
 * Rejects unaligned, empty, overflowing, or bootloader-overlapping writes. The source page
 * must reside in SRAM. Interrupts are disabled while the SDK temporarily exits XIP, programs
 * flash, and restores normal mapping. The entire programmed range is then compared against the
 * SRAM source before reporting success.
 */
bool storage_program(uint32_t offset, const uint8_t *data, uint32_t size);

/**
 * @brief Obtain a read pointer while XIP is enabled.
 *
 * @param[in] offset Flash byte offset already checked by the caller.
 * @return Pointer to the mapped flash byte.
 *
 * @details
 * Returns the XIP-mapped address corresponding to a flash byte offset. It does not change
 * flash mode or validate a range; callers must validate metadata and bounds before reading.
 * All reads through this pointer occur after the SDK erase/program routines have restored XIP
 * access.
 */
const uint8_t *storage_read(uint32_t offset);

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
bool application_valid(void);

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
uint8_t update_begin(update_t *state, uint32_t length);

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
uint8_t update_data(update_t *state, uint32_t offset, const uint8_t data[4]);

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
uint8_t update_finish(update_t *state, uint32_t expected_crc);

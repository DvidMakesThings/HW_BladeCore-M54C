/**
 * @file src/Software/CAN_Bootloader/incl/flash_storage.c
 *
 * @version 1.0.0
 * @date 2026-09-18
 *
 * @details
 * Provides the RP2354 backend for sector erase, page programming, and mapped flash reads. The
 * loader is linked with the SDK copy-to-RAM startup, leaving its active update code and data
 * in SRAM. Core 1 is not started, interrupts are masked during XIP-off flash operations, and
 * every operation is followed by complete readback verification. Bounds checks exclude the
 * resident bootloader region.
 *
 * @project CAN_Bootloader - BladeCore-M54C firmware
 * @github https://github.com/DvidMakesThings/HW_BladeCore-M54C
 */
#include "update.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/regs/addressmap.h"
#include <string.h>

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
const uint8_t *storage_read(uint32_t offset)
{
    return (const uint8_t *)(XIP_BASE + offset);
}

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
bool storage_erase(uint32_t offset, uint32_t size)
{
    if (offset < META_OFFSET || (offset & (FLASH_SECTOR_SIZE - 1)) || size == 0 ||
        (size & (FLASH_SECTOR_SIZE - 1)) || offset >= PICO_FLASH_SIZE_BYTES ||
        size > PICO_FLASH_SIZE_BYTES - offset)
    {
        return false;
    }
    const uint32_t interrupt_state = save_and_disable_interrupts(); /**< Saved PRIMASK. */
    /* Core 1 is never started. Code and source buffers are in SRAM while XIP is disabled. */
    flash_range_erase(offset, size);
    restore_interrupts(interrupt_state);

    const uint8_t *bytes = storage_read(offset);    /**< Flash readback after XIP restoration. */
    for (uint32_t index = 0; index < size; ++index) /**< Byte under verification. */
    {
        if (bytes[index] != 0xff)
        {
            return false;
        }
    }
    return true;
}

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
bool storage_program(uint32_t offset, const uint8_t *data, uint32_t size)
{
    if (offset < META_OFFSET || (offset & (FLASH_PAGE_SIZE - 1)) || size == 0 ||
        (size & (FLASH_PAGE_SIZE - 1)) || offset >= PICO_FLASH_SIZE_BYTES ||
        size > PICO_FLASH_SIZE_BYTES - offset)
    {
        return false;
    }
    const uint32_t interrupt_state = save_and_disable_interrupts(); /**< Saved PRIMASK. */
    flash_range_program(offset, data, size);
    restore_interrupts(interrupt_state);
    /* Never acknowledge a programmed page before comparing all bytes against SRAM. */
    return memcmp(storage_read(offset), data, size) == 0;
}

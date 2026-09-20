/**
 * @file src/Software/CAN_Bootloader/incl/update_test.c
 *
 * @version 1.0.0
 * @date 2026-09-18
 *
 * @details
 * Runs native fault-injection tests against the production protocol and update modules using a
 * simulated NOR flash array. It exercises CRC compatibility, image limits, ordering, duplicate
 * acknowledgements, partial-page padding, interrupted updates, corruption, invalid vectors,
 * and erase/program failures. A sentinel verifies that application updates never change the
 * resident bootloader. CMake excludes this harness from the embedded target even when the
 * generator rescans incl.
 *
 * @project CAN_Bootloader - BladeCore-M54C firmware
 * @github https://github.com/DvidMakesThings/HW_BladeCore-M54C
 */
#include "update.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t flash_memory[PICO_FLASH_SIZE_BYTES]; /**< Simulated NOR flash contents. */
static uint8_t image[4357];    /**< Test image spanning a sector and a partial final page. */
static unsigned program_count; /**< Number of successful simulated page programs. */
static bool erase_failure;     /**< Inject the next erase failure. */
static bool program_failure;   /**< Inject the next page-program failure. */
static bool readback_failure;  /**< Corrupt the next programmed page after returning success. */

/**
 * @brief Obtain a read pointer while XIP is enabled.
 *
 * @param[in] offset Flash byte offset already checked by the caller.
 * @return Pointer to the mapped flash byte.
 *
 * @details
 * Returns a pointer into the simulated NOR array after asserting the requested offset is
 * inside its bounds. This substitutes for XIP mapping while exercising the unchanged
 * production update state machine on the host.
 */
const uint8_t *storage_read(uint32_t offset)
{
    assert(offset < sizeof(flash_memory));
    return flash_memory + offset;
}

/**
 * @brief Erase a permitted flash range and verify every byte is erased.
 *
 * @param[in] offset Flash offset, aligned to a 4096-byte sector.
 * @param[in] size Nonzero sector-multiple byte count.
 * @return True on successful erase and readback; bootloader ranges are rejected; false otherwise.
 *
 * @details
 * Asserts sector alignment and protected-region bounds, then fills the simulated range with
 * erased bytes. An armed fault returns failure before mutation. The test harness uses this
 * hook to verify that metadata invalidation failures cannot start a transfer.
 */
bool storage_erase(uint32_t offset, uint32_t size)
{
    assert(offset >= META_OFFSET && !(offset & 4095) && !(size & 4095));
    assert(size <= sizeof(flash_memory) - offset);
    if (erase_failure)
    {
        erase_failure = false;
        return false;
    }
    memset(flash_memory + offset, 0xff, size);
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
 * Asserts page alignment and protected-region bounds, then enforces NOR one-to-zero bit
 * programming semantics. Optional faults reject the write or corrupt readback after reporting
 * success. The successful program counter detects accidental double-programming after
 * duplicate CAN data frames.
 */
bool storage_program(uint32_t offset, const uint8_t *data, uint32_t size)
{
    assert(offset >= META_OFFSET && !(offset & 255) && !(size & 255));
    assert(size <= sizeof(flash_memory) - offset);
    if (program_failure)
    {
        program_failure = false;
        return false;
    }
    for (uint32_t index = 0; index < size; ++index) /**< Simulated NOR programming position. */
    {
        assert((flash_memory[offset + index] & data[index]) == data[index]);
        flash_memory[offset + index] &= data[index];
    }
    ++program_count;
    if (readback_failure)
    {
        readback_failure = false;
        flash_memory[offset] ^= 1;
    }
    return true;
}

/**
 * @brief Reset the simulated flash and prepare a structurally valid image.
 *
 * @par Parameters
 * None (void).
 * @return None (void).
 *
 * @details
 * Initialises the simulated NOR flash to its erased state and fills the protected bootloader
 * region with a sentinel. A deterministic image is prepared with valid SRAM and XIP reset
 * vectors. All injected failures and page-program counters are cleared so each test starts
 * from a reproducible state.
 */
static void prepare(void)
{
    memset(flash_memory, 0xff, sizeof(flash_memory));
    memset(flash_memory, 0xa5, META_OFFSET); /* Sentinel detects any bootloader corruption. */
    for (unsigned index = 0; index < sizeof(image); ++index) /**< Test image byte position. */
    {
        image[index] = (uint8_t)(index * 37u);
    }
    put_u32(image, 0x20082000);
    put_u32(image + 4, APP_BASE + 0x101);
    program_count = 0;
    erase_failure = false;
    program_failure = false;
    readback_failure = false;
}

/**
 * @brief Send a complete image and optionally retry every frame.
 *
 * @param[in,out] state Active update state.
 * @param[in] duplicate True retries each accepted frame with identical bytes.
 * @return None (void); failed expectations abort the test process.
 *
 * @details
 * Sends a deterministic test image to the production update_data implementation in four-byte
 * chunks. The final frame is padded with erased bytes. When duplicate testing is enabled,
 * every accepted frame is repeated and the test asserts that the duplicate neither advances
 * the stream nor programs another flash page.
 */
static void stream(update_t *state, bool duplicate)
{
    for (uint32_t offset = 0; offset < sizeof(image); offset += 4) /**< Firmware byte offset. */
    {
        uint8_t data[4] = {0xff, 0xff, 0xff, 0xff}; /**< Padded final CAN payload. */
        uint32_t count = sizeof(image) - offset;    /**< Remaining meaningful image bytes. */
        if (count > sizeof(data))
        {
            count = sizeof(data);
        }
        memcpy(data, image + offset, count);
        assert(update_data(state, offset, data) == STATUS_OK);
        if (duplicate)
        {
            const unsigned before = program_count; /**< Program count before duplicate retry. */
            assert(update_data(state, offset, data) == STATUS_OK);
            assert(program_count == before);
        }
    }
}

/**
 * @brief Exercise valid updates, boundary rejection, retries, and storage faults.
 *
 * @par Parameters
 * None (void).
 * @return Zero when all invariants hold; assertions terminate on failure.
 *
 * @details
 * Exercises the production update state machine against simulated NOR flash, including lost-
 * ACK retransmissions, out-of-order offsets, corrupt checksums, bad reset vectors, partial
 * pages, and injected storage failures. Assertions verify that only a completely verified
 * manifest becomes bootable and that the protected bootloader sentinel never changes. The
 * process prints one PASS summary when all checks succeed.
 */
int main(void)
{
    update_t state = {0};                 /**< State under test. */
    const uint8_t data[4] = {0, 1, 2, 3}; /**< Arbitrary out-of-order frame payload. */
    prepare();
    assert(crc32("123456789", 9) == 0xcbf43926u);
    assert(!application_valid());
    assert(update_begin(&state, 255) == STATUS_ARGUMENT);
    assert(update_begin(&state, APP_MAX_SIZE + 1) == STATUS_ARGUMENT);
    assert(update_data(&state, 0, data) == STATUS_STATE);
    assert(update_begin(&state, sizeof(image)) == STATUS_OK);
    assert(update_data(&state, 4, data) == STATUS_SEQUENCE);
    assert(update_data(&state, UINT32_MAX, data) == STATUS_SEQUENCE);
    assert(update_finish(&state, 0) == STATUS_STATE);
    stream(&state, true);
    assert(update_data(&state, 0, data) == STATUS_SEQUENCE);
    assert(!application_valid()); /* Complete data alone is never bootable. */
    assert(update_finish(&state, 0) == STATUS_CRC);
    assert(!application_valid());
    assert(update_finish(&state, crc32(image, sizeof(image))) == STATUS_OK);
    assert(application_valid());
    assert(memcmp(storage_read(APP_OFFSET), image, sizeof(image)) == 0);
    assert(storage_read(APP_OFFSET)[sizeof(image)] == 0xff);
    assert(update_finish(&state, 0) == STATUS_STATE);
    flash_memory[APP_OFFSET + 300] ^= 1;
    assert(!application_valid());
    flash_memory[APP_OFFSET + 300] ^= 1;
    flash_memory[META_OFFSET + 20] ^= 1;
    assert(!application_valid());

    /* Beginning another transfer must invalidate the old image before any data. */
    assert(update_begin(&state, sizeof(image)) == STATUS_OK);
    assert(!application_valid());
    assert(update_data(&state, 0, image) == STATUS_OK);
    assert(update_data(&state, 0, data) == STATUS_SEQUENCE);
    assert(!application_valid()); /* Simulates reset before a first page is committed. */

    /* Fail metadata erase and first application page programming separately. */
    erase_failure = true;
    assert(update_begin(&state, sizeof(image)) == STATUS_FLASH);
    assert(!state.active);
    assert(update_begin(&state, sizeof(image)) == STATUS_OK);
    for (uint32_t offset = 0; offset < 252; offset += 4) /**< Fill first page except last word. */
    {
        assert(update_data(&state, offset, image + offset) == STATUS_OK);
    }
    program_failure = true;
    assert(update_data(&state, 252, image + 252) == STATUS_FLASH);
    assert(!application_valid() && !state.active);

    /* The state machine must detect a backend that lies about a successful write. */
    assert(update_begin(&state, sizeof(image)) == STATUS_OK);
    for (uint32_t offset = 0; offset < 252; offset += 4) /**< Fill page for readback fault. */
    {
        assert(update_data(&state, offset, image + offset) == STATUS_OK);
    }
    readback_failure = true;
    assert(update_data(&state, 252, image + 252) == STATUS_FLASH);
    assert(!application_valid());

    prepare();
    put_u32(image + 4, 0x10000001); /* Reset vector points into the bootloader. */
    assert(update_begin(&state, sizeof(image)) == STATUS_OK);
    stream(&state, false);
    assert(update_finish(&state, crc32(image, sizeof(image))) == STATUS_IMAGE);
    assert(!application_valid());

    prepare();
    assert(update_begin(&state, sizeof(image)) == STATUS_OK);
    stream(&state, false);
    program_failure = true;
    assert(update_finish(&state, crc32(image, sizeof(image))) == STATUS_FLASH);
    assert(!application_valid()); /* Torn manifest cannot make the image valid. */
    for (unsigned index = 0; index < META_OFFSET; ++index) /**< Protected bootloader byte. */
    {
        assert(flash_memory[index] == 0xa5);
    }
    puts("PASS: CRC, bounds, ordering, retries, partial page, interrupted update, corruption, "
         "flash faults, vectors, bootloader protection");
    return 0;
}

/**
 * @file src/Software/BaReTOS/incl/protocol/protocol.c
 *
 * @version 1.0.0
 * @date 2026-09-18
 *
 * @details
 * Implements explicit little-endian conversions, reflected IEEE CRC-32, and structural checks
 * for persistent manifests and XIP application vectors. These helpers are shared as
 * independent source copies by the RTOS application and standalone loader. They contain no
 * scheduler or peripheral dependencies and are also compiled into the native fault-injection
 * tests.
 *
 * @project BaReTOS - BladeCore-M54C firmware
 * @github https://github.com/DvidMakesThings/HW_BladeCore-M54C
 */
#include "protocol.h"

/**
 * @brief Decode an unaligned little-endian word.
 *
 * @param[in] bytes At least four readable bytes.
 * @return Decoded unsigned word.
 *
 * @details
 * Combines four bytes explicitly, starting with the least significant byte. This avoids
 * alignment-dependent word loads when decoding CAN payloads or flash image headers. The caller
 * must provide four accessible bytes; this helper does not perform bounds checking.
 */
uint32_t get_u32(const uint8_t *bytes)
{
    /* Byte-wise decoding also works on unaligned CAN payloads. */
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

/**
 * @brief Encode an unaligned little-endian word.
 *
 * @param[out] bytes At least four writable bytes.
 * @param[in] value Word to encode.
 * @return None (void).
 *
 * @details
 * Writes the least significant byte first using byte-sized stores. This helper is used for CAN
 * offsets and image metadata so host and device agree on byte order regardless of pointer
 * alignment. The destination must provide four writable bytes.
 */
void put_u32(uint8_t *bytes, uint32_t value)
{
    for (unsigned index = 0; index < 4; ++index) /**< Output byte position. */
    {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

/**
 * @brief Calculate reflected IEEE CRC-32 with initial and final XOR 0xFFFFFFFF.
 *
 * @param[in] data Input bytes; NULL is permitted only when length is zero.
 * @param[in] length Number of bytes to process.
 * @return The checksum, compatible with Python zlib.crc32.
 *
 * @details
 * Processes each input byte using the reflected IEEE polynomial 0xEDB88320. The initial
 * accumulator and final XOR are both 0xFFFFFFFF, matching zlib.crc32 on the host. The table-
 * free implementation has no lookup table that could remain in XIP flash during recovery.
 */
uint32_t crc32(const void *data, size_t length)
{
    const uint8_t *bytes = data;     /**< Next byte to process. */
    uint32_t checksum = 0xffffffffu; /**< Reflected CRC accumulator. */

    /* A table-free implementation needs no flash-resident lookup table. */
    while (length--)
    {
        checksum ^= *bytes++;
        for (unsigned bit = 0; bit < 8; ++bit) /**< Current bit position. */
        {
            checksum = (checksum >> 1) ^ (0xedb88320u & (0u - (checksum & 1u)));
        }
    }
    return ~checksum;
}

/**
 * @brief Check manifest identity, bounds, and header checksum.
 *
 * @param[in] manifest Aligned manifest object copied from flash.
 * @return True if its fields are safe to use for image validation; false otherwise.
 *
 * @details
 * Checks the commit signature, format version, board compatibility marker, and firmware length
 * before accepting the stored header checksum. This is only structural manifest validation;
 * application_valid additionally checks the image vectors and the complete application CRC
 * before allowing a boot.
 */
bool manifest_valid(const image_manifest_t *manifest)
{
    /* Validate length before callers use it to read application flash. */
    return manifest->magic == META_MAGIC && manifest->version == PROTOCOL_VERSION &&
           manifest->board == BOARD_TYPE && manifest->length >= 256 &&
           manifest->length <= APP_MAX_SIZE &&
           manifest->header_crc == crc32(manifest, offsetof(image_manifest_t, header_crc));
}

/**
 * @brief Check that an XIP application's initial vectors lie in valid memory.
 *
 * @param[in] image Start of the application image.
 * @param[in] length Exact application length.
 * @return True if MSP is aligned in SRAM and the Thumb reset handler is in the image; false
 * otherwise.
 *
 * @details
 * Checks the initial main stack pointer against the RP2354 SRAM range and requires eight-byte
 * alignment. The reset vector must have its Thumb bit set and point inside the declared XIP
 * application image. This rejects images linked at the bootloader address or for an
 * incompatible memory layout; it is not cryptographic authentication.
 */
bool image_vectors_valid(const uint8_t *image, uint32_t length)
{
    if (length < 256 || length > APP_MAX_SIZE)
    {
        return false;
    }
    const uint32_t stack = get_u32(image);     /**< Initial main stack pointer. */
    const uint32_t reset = get_u32(image + 4); /**< Thumb reset handler. */

    /* The stack may equal the top of SRAM; reset must address an image byte. */
    return stack > 0x20000000u && stack <= 0x20082000u && !(stack & 7u) && (reset & 1u) &&
           (reset & ~1u) >= APP_BASE && (reset & ~1u) < APP_BASE + length;
}

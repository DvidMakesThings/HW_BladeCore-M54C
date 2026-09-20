/**
 * @file src/Software/BaReTOS/incl/protocol/protocol.h
 *
 * @version 1.0.0
 * @date 2026-09-18
 *
 * @details
 * Declares the scheduler-independent CAN frame format, management commands, result codes, and
 * persistent image manifest. The protocol uses classic eight-byte CAN frames with extended
 * identifiers. Full silicon identity selection precedes destructive operations; abbreviated
 * node addresses alone are not sufficient. Numeric protocol and flash-layout macros remain in
 * root CONFIG.h.
 *
 * @project BaReTOS - BladeCore-M54C firmware
 * @github https://github.com/DvidMakesThings/HW_BladeCore-M54C
 */
#pragma once
#include "CONFIG.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Command byte used by control requests and acknowledgements. */
typedef enum
{
    CMD_INFO = 1,   /**< Discover identity, operating mode, and protocol version. */
    CMD_ENTER = 2,  /**< Acknowledge transition into recovery mode. */
    CMD_BEGIN = 3,  /**< Invalidate the previous image and accept a new length. */
    CMD_DATA = 4,   /**< Acknowledge an offset-addressed data frame. */
    CMD_FINISH = 5, /**< Verify the complete image and commit its manifest. */
    CMD_BOOT = 6,   /**< Restart into a verified application. */
    CMD_ABORT = 7,  /**< Abandon an incomplete transfer. */
    CMD_SELECT = 8  /**< Acknowledge selection by complete silicon ID. */
} protocol_command_t;

/** @brief Update operation result. */
typedef enum
{
    STATUS_OK = 0,       /**< Operation succeeded. */
    STATUS_ARGUMENT = 1, /**< Invalid command or size. */
    STATUS_STATE = 2,    /**< No active transfer or incomplete image. */
    STATUS_SEQUENCE = 3, /**< Unexpected offset or conflicting retransmission. */
    STATUS_FLASH = 4,    /**< Flash erase, programming, or readback failed. */
    STATUS_CRC = 5,      /**< Image checksum did not match. */
    STATUS_IMAGE = 6     /**< Invalid image structure or persistent manifest. */
} protocol_status_t;

/** @brief Persistent image validity record, committed last. */
typedef struct
{
    uint32_t magic;      /**< Required META_MAGIC signature. */
    uint32_t version;    /**< Manifest layout version. */
    uint32_t board;      /**< Board compatibility marker. */
    uint32_t length;     /**< Exact application byte count before page padding. */
    uint32_t crc;        /**< IEEE CRC-32 of the application. */
    uint32_t header_crc; /**< IEEE CRC-32 over the preceding five words. */
} image_manifest_t;

/** @brief Classic CAN frame independent of the scheduler and SPI driver. */
typedef struct
{
    uint32_t id;     /**< Eleven-bit or twenty-nine-bit arbitration identifier. */
    uint8_t data[8]; /**< Payload storage. */
    uint8_t dlc;     /**< Payload length from zero through eight. */
    bool extended;   /**< True for twenty-nine-bit identifiers. */
    bool rtr;        /**< True for a remote transmission request. */
} can_frame_t;

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
uint32_t get_u32(const uint8_t *bytes);

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
void put_u32(uint8_t *bytes, uint32_t value);

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
uint32_t crc32(const void *data, size_t length);

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
bool manifest_valid(const image_manifest_t *manifest);

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
bool image_vectors_valid(const uint8_t *image, uint32_t length);

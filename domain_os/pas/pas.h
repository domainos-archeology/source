/*
 * PAS_$ - Process Address Space bitmap operations
 *
 * This subsystem provides operations for manipulating process address
 * space bitmaps, used for tracking allocation and availability of
 * address space regions.
 *
 * Reverse engineered from Domain/OS
 */

#ifndef PAS_H
#define PAS_H

#include "os/os.h"

/*
 * PAS_$SET_BUILD - Initialize a bitmap and set a range of bits
 *
 * Copies a bitmap template and then sets bits within a specified range.
 * The bitmap is stored in big-endian bit order (bit 0 at highest address).
 *
 * Parameters:
 *   dest      - Destination bitmap to initialize
 *   src       - Source bitmap template to copy from
 *   start_bit - First bit to set (clamped to >= 0)
 *   end_bit   - Last bit to set (clamped to <= total_bits)
 *   total_bits - Total number of bits in the bitmap
 *
 * Note: The bitmap uses big-endian bit ordering where higher-numbered
 * bits are stored at lower byte addresses. This matches M68K's bit
 * numbering convention.
 */
void PAS_$SET_BUILD(
    uint16_t *dest,
    uint16_t *src,
    int16_t start_bit,
    int16_t end_bit,
    uint16_t total_bits
);

#endif /* PAS_H */

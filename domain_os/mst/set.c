/*
 * MST_$SET - Set a bit in a bitmap
 *
 * This function sets a single bit in a bitmap structure. The bitmap is
 * organized in big-endian bit order within bytes, with the highest-numbered
 * bit at position 0 within the byte array.
 *
 * The bit indexing formula converts a logical bit index to a byte offset
 * and bit position within that byte:
 *   byte_offset = ((size - 1) | 0xf) - bit_index) >> 3
 *   bit_position = bit_index & 7
 *
 * This unusual formula accounts for the m68k big-endian bit numbering
 * and the 16-bit aligned bitmap structure.
 *
 * Used primarily for the ASID allocation bitmap where setting a bit
 * indicates an ASID is in use.
 */

#include "mst.h"

/*
 * MST_$SET - Set a bit in a bitmap
 *
 * @param bitmap    Pointer to the bitmap array
 * @param size      Size of the bitmap (number of bits, rounded up to 16)
 * @param bit_index Bit index to set (0-based)
 */
void MST_$SET(void *bitmap, uint16_t size, uint16_t bit_index)
{
    uint8_t *bytes = (uint8_t *)bitmap;

    /*
     * Calculate byte offset within bitmap.
     * The formula ((size - 1) | 0xf) rounds size up to next multiple of 16,
     * minus 1. Then subtracting bit_index and shifting right by 3 gives
     * the byte offset from the start of the bitmap.
     *
     * This handles big-endian bit ordering where bit 0 is at the "end"
     * of the bitmap in memory layout terms.
     */
    uint16_t byte_offset = (uint16_t)(((size - 1) | 0x0f) - bit_index) >> 3;

    /*
     * Set the bit using bit position within byte.
     * bit_index & 7 gives position 0-7 within the byte.
     */
    bytes[byte_offset] |= (uint8_t)(1 << (bit_index & 7));
}

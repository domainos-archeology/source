/*
 * MST_$SET_CLEAR - Clear a bit in a bitmap
 *
 * This function clears a single bit in a bitmap structure. The bitmap is
 * organized in big-endian bit order within bytes, with the highest-numbered
 * bit at position 0 within the byte array.
 *
 * The bit indexing formula converts a logical bit index to a byte offset
 * and bit position within that byte (same as MST_$SET):
 *   byte_offset = ((size - 1) | 0xf) - bit_index) >> 3
 *   bit_position = bit_index & 7
 *
 * Used primarily for the ASID deallocation where clearing a bit
 * indicates an ASID is free for reuse.
 */

#include "mst.h"

/*
 * MST_$SET_CLEAR - Clear a bit in a bitmap
 *
 * @param bitmap    Pointer to the bitmap array
 * @param size      Size of the bitmap (number of bits, rounded up to 16)
 * @param bit_index Bit index to clear (0-based)
 */
void MST_$SET_CLEAR(void *bitmap, uint16_t size, uint16_t bit_index)
{
    uint8_t *bytes = (uint8_t *)bitmap;

    /*
     * Calculate byte offset within bitmap.
     * Same formula as MST_$SET - handles big-endian bit ordering.
     */
    uint16_t byte_offset = (uint16_t)(((size - 1) | 0x0f) - bit_index) >> 3;

    /*
     * Clear the bit using bit position within byte.
     * bit_index & 7 gives position 0-7 within the byte.
     */
    bytes[byte_offset] &= (uint8_t)~(1 << (bit_index & 7));
}

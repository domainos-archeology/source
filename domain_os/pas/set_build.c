/*
 * PAS_$SET_BUILD - Initialize a bitmap and set a range of bits
 *
 * Reverse engineered from Domain/OS at address 0x00e11fa8
 */

#include "pas/pas.h"

/*
 * PAS_$SET_BUILD
 *
 * This function initializes a process address space bitmap by:
 * 1. Copying a template bitmap from source to destination
 * 2. Setting bits within a specified range
 *
 * The bitmap uses big-endian bit ordering:
 * - Bit 0 is stored at the highest byte address
 * - Bit N is stored at lower byte addresses
 *
 * This is typical for M68K architecture where bit 0 is the LSB.
 */
void PAS_$SET_BUILD(
    uint16_t *dest,
    uint16_t *src,
    int16_t start_bit,
    int16_t end_bit,
    uint16_t total_bits)
{
    uint16_t *dest_ptr;
    int16_t num_words;
    int16_t bit_pos;
    int16_t clamped_end;
    uint16_t max_bit_index;
    uint8_t *byte_ptr;
    int16_t byte_offset;

    /*
     * Copy the template bitmap
     * Number of words to copy is (total_bits / 16), rounded up
     * The dbf loop copies (count + 1) words
     */
    num_words = (int16_t)(total_bits >> 4);
    dest_ptr = dest;

    do {
        *dest_ptr = *src;
        num_words--;
        src++;
        dest_ptr++;
    } while (num_words != -1);

    /*
     * Clamp start_bit to be non-negative
     */
    if (start_bit < 0) {
        start_bit = 0;
    }

    /*
     * Clamp end_bit to not exceed total_bits
     */
    clamped_end = end_bit;
    if (total_bits < end_bit) {
        clamped_end = (int16_t)total_bits;
    }

    /*
     * Calculate the max bit index (round up total_bits to next 16-bit boundary - 1)
     * This gives us the byte range for the bitmap
     */
    max_bit_index = total_bits | 0x000F;

    /*
     * Set bits from start_bit to clamped_end
     * Uses big-endian bit ordering within the bitmap
     */
    for (bit_pos = start_bit; bit_pos <= clamped_end; bit_pos++) {
        /*
         * Calculate byte offset from the start of the bitmap
         * Higher bit numbers are at lower offsets (big-endian bit order)
         */
        byte_offset = (int16_t)((max_bit_index - bit_pos) >> 3);

        /*
         * Set the appropriate bit within that byte
         * Bit position within byte is (bit_pos & 7)
         */
        byte_ptr = (uint8_t *)dest + byte_offset;
        *byte_ptr |= (1 << (bit_pos & 7));
    }
}

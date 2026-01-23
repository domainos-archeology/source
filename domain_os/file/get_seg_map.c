/*
 * FILE_$GET_SEG_MAP - Get segment map for a file
 *
 * Original address: 0x00E74044
 * Size: 204 bytes
 *
 * Retrieves the segment map for a file, showing which segments are
 * currently allocated/mapped. The output is a bitmask where each
 * bit represents a segment.
 *
 * Assembly analysis:
 *   - link.w A6,-0x40         ; Large stack frame
 *   - Saves D2-D5, A2, A3, A5 to stack
 *   - Copies file_uid to local buffer (8 bytes at -0x28, -0x24)
 *   - Clears output bitmap (param_4)
 *   - Checks param_3 sign for flags
 *   - Calls AST_$GET_SEG_MAP with:
 *     - uid (local copy)
 *     - start offset (*param_2)
 *     - unused (0)
 *     - vol_uid (1)
 *     - count (0x20 = 32 segments)
 *     - flags (1 or 0 based on param_3 sign)
 *     - output buffer (32 bytes)
 *     - status pointer
 *   - Loops through returned 32-bit words, converting bit positions
 *   - Copies status to param_5
 */

#include "file/file_internal.h"

/*
 * FILE_$GET_SEG_MAP - Get segment map for a file
 *
 * Retrieves a bitmap showing which segments of a file are currently
 * allocated or mapped in the active segment table. Each bit in the
 * output corresponds to a segment.
 *
 * Parameters:
 *   file_uid    - UID of file to query
 *   start_off   - Pointer to starting offset for segment query
 *   flags_in    - Pointer to flags (negative = set flag bit 0)
 *   bitmap_out  - Output bitmap (32 bits)
 *   status_ret  - Output status code
 *
 * The function calls AST_$GET_SEG_MAP to retrieve segment information
 * for 32 segments starting at the specified offset, then converts
 * the returned segment flags into a bitmap format.
 */
void FILE_$GET_SEG_MAP(uid_t *file_uid, uint32_t *start_off,
                       int8_t *flags_in, uint32_t *bitmap_out,
                       status_$t *status_ret)
{
    uid_t local_uid;
    uint32_t seg_output[8];  /* 32 bytes for segment data */
    status_$t local_status;
    uint16_t flags;
    int16_t word_idx;
    int16_t bit_offset;
    uint32_t bit_idx;
    uint32_t i;

    /* Copy UID to local buffer */
    local_uid.high = file_uid->high;
    local_uid.low = file_uid->low;

    /* Clear output bitmap */
    *bitmap_out = 0;

    /* Set flags based on param_3 sign (negative = 1, positive = 0) */
    flags = (*flags_in < 0) ? 1 : 0;

    /*
     * Call AST_$GET_SEG_MAP to get segment information
     * Parameters:
     *   uid_info     - pointer to UID
     *   start_offset - from param_2
     *   unused       - 0
     *   vol_uid      - 1
     *   count        - 0x20 (32 segments)
     *   flags        - calculated above
     *   output       - local buffer
     *   status       - local status
     */
    AST_$GET_SEG_MAP((uint32_t *)&local_uid, *start_off, 0, (uid_t *)1,
                     0x20, flags, seg_output, &local_status);

    /*
     * Convert segment flags to bitmap
     * The assembly loops through the output (as 8 longwords) and
     * for each set bit in the AST output, sets the corresponding
     * bit in the output bitmap.
     *
     * The bit positions are mapped from AST output format to a
     * linear 32-bit bitmap.
     */
    bit_offset = 0;
    for (word_idx = 0; word_idx >= 0; word_idx--) {  /* Loop counter starts at 0 */
        uint32_t seg_word = seg_output[word_idx < 8 ? word_idx : 0];

        if (seg_word != 0) {
            /* Process each bit in this word */
            for (bit_idx = 0; bit_idx < 32; bit_idx++) {
                if ((bit_idx < 32) && (seg_word & (1 << bit_idx))) {
                    uint16_t out_bit = bit_offset + (uint16_t)bit_idx;

                    /* Set corresponding bit in output */
                    if (out_bit < 32) {
                        uint32_t temp = 0;
                        uint16_t byte_offset = (31 - out_bit) >> 3;
                        uint8_t *temp_ptr = ((uint8_t *)&temp) + byte_offset;
                        *temp_ptr |= (1 << (out_bit & 7));
                        *bitmap_out |= temp;
                    }
                }
            }
        }

        bit_offset += 32;
        if (word_idx == 0) break;  /* Exit after first iteration */
    }

    *status_ret = local_status;
}

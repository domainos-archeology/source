/*
 * dir_$calc_entry_size - Compute the aligned size of a directory entry
 *
 * Given a pointer to a directory entry, computes the total size including
 * the type-dependent header and the variable-length name field. For soft
 * link entries (type 4) with inline overflow data (overflow_page == -1),
 * the link data length is also included.
 *
 * The result is rounded up to the nearest 4-byte boundary.
 *
 * Original address: 0x00E4EF42
 * Size: 58 bytes
 *
 * Entry layout (first 6 bytes):
 *   byte  0: flags/type (type = bits 0-2)
 *   byte  1: name length
 *   bytes 2-3: link data length (type 4 only, when inline)
 *   bytes 4-5: overflow page index (-1 = inline data follows name)
 */

#include "dir/dir_internal.h"

uint16_t dir_$calc_entry_size(uint8_t *entry) {
    uint8_t entry_type = entry[0] & 7;

    /* Start with name length (byte 1) plus type-dependent header size */
    int16_t size = DIR_$NAME_OFFSET_TABLE[entry_type] + (uint16_t)entry[1];

    /* For soft link entries with inline data (overflow_page == -1),
     * add the link data length stored at offset 2 */
    if (entry_type == 4 && *(int16_t *)(entry + 4) == -1) {
        size += *(int16_t *)(entry + 2);
    }

    /* Round up to 4-byte alignment */
    return (uint16_t)((size + 3) & ~3);
}

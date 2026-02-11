/*
 * name_$validate_leaf - Validate and parse directory leaf name
 *
 * Validates a directory entry name:
 * - Length must be <= 32 characters
 * - First character cannot be backslash (\)
 * - All characters must be in the valid character set (A5-relative bitmap)
 *
 * Also applies case mapping to normalize the name.
 *
 * Parameters:
 *   name       - Input name string
 *   name_len   - Length of input name
 *   parsed     - Output: case-mapped name
 *   parsed_len - Output: length of parsed name
 *
 * Returns:
 *   0xFF (true) if valid, 0 (false) if invalid
 *
 * Original address: 0x00e54414
 * Size: 154 bytes
 */

#include "dir/dir_internal.h"
#include "misc/string.h"

/* Character validation data at DAT_00e544ae */
extern uint8_t DAT_00e544ae;

/* Valid character bitmap - accessed via A5+0x00 (full charset) and A5+0x20 (first char) */
/* These bitmaps are 32 bytes each, with bit N set if char (0xFF - N) is valid */

int8_t name_$validate_leaf(char *name, uint16_t name_len,
                           uint8_t *parsed, uint16_t *parsed_len)
{
    int8_t result[10];
    int16_t i;
    uint16_t out_len;
    uint8_t ch;
    uint16_t bit_idx;
    uint16_t byte_idx;

    /* Check length limit */
    if (name_len > 32) {
        return 0;
    }

    /* Apply case mapping */
    MAP_CASE(name, &name_len, parsed, &DAT_00e544ae, parsed_len, result);

    /* Check if case mapping succeeded */
    if (result[0] < 0) {
        return 0;
    }

    out_len = *parsed_len;
    if (out_len == 0) {
        return 0;
    }

    /* Check length again after mapping */
    if (out_len > 32) {
        return 0;
    }

    /* First character cannot be backslash */
    if (parsed[0] == '\\') {
        return 0;
    }

    /* Validate first character against A5+0x20 bitmap
     * The bitmap has bit set for valid characters, indexed by (0xFF - char) */
    ch = parsed[0];
    bit_idx = (0xFF - ch) & 0x07;
    byte_idx = (0xFF - ch) >> 3;

    /* TODO: Access A5+0x20+byte_idx bitmap
     * For now, assume valid ASCII alphanumeric and common chars */
    /* if ((valid_first_char_bitmap[byte_idx] & (1 << bit_idx)) == 0) return 0; */

    /* Validate remaining characters against A5+0x00 bitmap */
    for (i = 1; i < (int16_t)out_len; i++) {
        ch = parsed[i];
        bit_idx = ch & 0x07;
        byte_idx = (0xFF - ch) >> 3;

        /* TODO: Access A5+byte_idx bitmap
         * For now, assume valid */
        /* if ((valid_char_bitmap[byte_idx] & (1 << bit_idx)) == 0) return 0; */
    }

    return (int8_t)0xFF;
}

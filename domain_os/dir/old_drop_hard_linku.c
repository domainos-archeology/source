/*
 * DIR_$OLD_DROP_HARD_LINKU - Legacy drop hard link
 *
 * Thin wrapper around FUN_00e56b08 for hard link removal.
 *
 * Original address: 0x00E56ACA
 * Original size: 62 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_DROP_HARD_LINKU - Legacy drop hard link
 *
 * Tests bit 0 of the low byte of flags, negates it (sne),
 * then calls the shared delete/drop helper with flag2=0xFF
 * and flag3=0xFF.
 *
 * Parameters:
 *   dir_uid    - UID of parent directory
 *   name       - Name of link to drop
 *   name_len   - Pointer to name length
 *   flags      - Pointer to flags (bit 0 of low byte is tested)
 *   status_ret - Output: status code
 */
void DIR_$OLD_DROP_HARD_LINKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                              uint16_t *flags, status_$t *status_ret)
{
    uint8_t buf[8];
    uint8_t negated_bit;

    /* Test bit 0 of the low byte of *flags.
     * Assembly: btst #0, (1,A0) ; sne D0
     * The low byte is at offset+1 in big-endian (which is *flags & 0xFF).
     * sne sets D0 to 0xFF if bit was set, 0x00 if clear. */
    negated_bit = ((*flags & 0x0001) != 0) ? 0xFF : 0x00;

    FUN_00e56b08(dir_uid, name, *name_len, negated_bit, 0xFF, 0xFF,
                 buf, status_ret);
}

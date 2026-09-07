/*
 * DIR_$OLD_DROP_HARD_LINKU - Legacy drop hard link
 *
 * Thin wrapper around NAME_$OLD_DELETE_ENTRYU for hard link removal.
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
    uint8_t buf[8];         /* A6-0x08, `pea (-0x8,A6)` at 0x00E56ADA */
    boolean check_del_right;

    /* 0x00E56AE6 `btst.b #0x0,(0x1,A0)` + `sne`: bit 0 of the LOW byte of
     * the flags word, i.e. *flags & 1, as a Domain boolean. */
    check_del_right = ((*flags & 0x0001) != 0) ? true : false;

    /* 0x00E56ADE / 0x00E56AE0: two `st` pushes - no_lock and allow_link are
     * both a constant TRUE. */
    NAME_$OLD_DELETE_ENTRYU(dir_uid, name, *name_len, check_del_right,
                            true, true, buf, status_ret);
}

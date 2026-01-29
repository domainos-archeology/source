/*
 * DIR_$OLD_ROOT_ADDU - Legacy add entry to root directory
 *
 * Validates that the directory is the root, then calls the
 * root add entry helper.
 *
 * Original address: 0x00E569AA
 * Original size: 90 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_ROOT_ADDU - Legacy add entry to root directory
 *
 * Compares dir_uid against NAME_$ROOT_UID. If it matches,
 * calls FUN_00e56682 to add the entry with type=2.
 * Otherwise returns status_$naming_not_root_dir.
 *
 * Parameters:
 *   dir_uid    - UID of root directory
 *   name       - Name for the entry
 *   name_len   - Pointer to name length
 *   file_uid   - UID of file to add
 *   flags      - Pointer to flags
 *   status_ret - Output: status code
 */
void DIR_$OLD_ROOT_ADDU(uid_t *dir_uid, char *name, int16_t *name_len,
                        uid_t *file_uid, uint32_t *flags, status_$t *status_ret)
{
    /* Verify this is the root directory */
    if (dir_uid->high == NAME_$ROOT_UID.high &&
        dir_uid->low == NAME_$ROOT_UID.low) {
        /* Call root add entry helper with type=2 */
        FUN_00e56682(dir_uid, 2, name, (uint16_t)*name_len,
                     file_uid, *flags, status_ret);
    } else {
        *status_ret = status_$naming_not_root_dir;
    }
}

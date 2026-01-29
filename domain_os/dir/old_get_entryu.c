/*
 * DIR_$OLD_GET_ENTRYU - Legacy get directory entry by name
 *
 * Dispatches to root or non-root entry lookup based on
 * comparison with NAME_$ROOT_UID.
 *
 * Original address: 0x00E58056
 * Original size: 110 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_GET_ENTRYU - Legacy get directory entry by name
 *
 * Makes a local copy of dir_uid, then compares against NAME_$ROOT_UID.
 * If the directory is root, calls FUN_00e57f74 (root lookup).
 * Otherwise calls FUN_00e57ce0 (non-root lookup).
 *
 * Parameters:
 *   dir_uid    - UID of directory to search
 *   name       - Name to look up
 *   name_len   - Pointer to name length
 *   entry_ret  - Output: entry information
 *   status_ret - Output: status code
 */
void DIR_$OLD_GET_ENTRYU(uid_t *dir_uid, char *name, uint16_t *name_len,
                         void *entry_ret, status_$t *status_ret)
{
    uid_t local_uid;

    /* Make local copy of directory UID */
    local_uid.high = dir_uid->high;
    local_uid.low = dir_uid->low;

    /* Dispatch based on whether this is the root directory */
    if (local_uid.high == NAME_$ROOT_UID.high &&
        local_uid.low == NAME_$ROOT_UID.low) {
        /* Root directory - use root lookup */
        FUN_00e57f74(&local_uid, name, *name_len, entry_ret, status_ret);
    } else {
        /* Non-root directory - use standard lookup */
        FUN_00e57ce0(&local_uid, name, *name_len, entry_ret, status_ret);
    }
}

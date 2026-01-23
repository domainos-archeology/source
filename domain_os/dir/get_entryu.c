/*
 * DIR_$GET_ENTRYU - Get a directory entry by name
 *
 * Retrieves information about a named entry in a directory.
 *
 * Original address: 0x00E4D500
 * Original size: 114 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$GET_ENTRYU - Get a directory entry by name
 *
 * Looks up a directory entry by name and returns information about it.
 *
 * Parameters:
 *   dir_uid    - UID of directory to search
 *   name       - Name to look up
 *   name_len   - Pointer to name length (max 255)
 *   entry_ret  - Output: entry information
 *   status_ret - Output: status code
 */
void DIR_$GET_ENTRYU(uid_t *dir_uid, char *name, uint16_t *name_len,
                     void *entry_ret, status_$t *status_ret)
{
    uid_t local_uid;
    uint16_t len;

    /* Make local copy of directory UID */
    local_uid.high = dir_uid->high;
    local_uid.low = dir_uid->low;

    /* Get name length */
    len = *name_len;

    /* Validate name length */
    if (len == 0 || len > DIR_MAX_LEAF_LEN) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* Call internal helper function */
    DIR_$GET_ENTRYU_FUN_00e4d460(status_ret);

    /* Check for fallback conditions */
    if (*status_ret == status_$file_bad_reply_received_from_remote_node ||
        *status_ret == status_$naming_bad_directory) {
        /* Fall back to old implementation */
        DIR_$OLD_GET_ENTRYU(&local_uid, name, name_len, entry_ret, status_ret);
    }
}

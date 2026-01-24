/*
 * DIR_$DIR_READU - Read directory entries
 *
 * Reads entries from a directory starting at the given position.
 *
 * Original address: 0x00E4E3A6
 * Original size: 116 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$DIR_READU - Read directory entries
 *
 * Reads entries from a directory. For the canned replicated root,
 * uses a special handler. For normal directories, tries the new
 * protocol first, then falls back to the old implementation.
 *
 * Parameters:
 *   dir_uid       - UID of directory to read
 *   entries_ret   - Output: array of directory entries
 *   entries_size  - Size of entries buffer
 *   continuation  - In/out: continuation position
 *   max_entries   - Pointer to max entries to return
 *   count_ret     - Pointer to count of entries returned
 *   flags         - Read flags
 *   eof_ret       - Pointer to EOF indicator
 *   status_ret    - Output: status code
 */
void DIR_$DIR_READU(uid_t *dir_uid, void *entries_ret, void *entries_size,
                    int32_t *continuation, uint16_t *max_entries,
                    int32_t *count_ret, void *flags, int32_t *eof_ret,
                    status_$t *status_ret)
{
    /* Check if max_entries is valid */
    if (*count_ret < 1) {
        *status_ret = status_$naming_object_is_not_an_acl_object;
        return;
    }

    /* Check if this is the canned replicated root */
    if (dir_uid->high == NAME_$CANNED_REP_ROOT_UID.high &&
        dir_uid->low == NAME_$CANNED_REP_ROOT_UID.low) {
        /* Use canned root handler */
        DIR_$DIR_READU_FUN_00e4e1a8(status_ret);
    } else {
        /* Try new protocol first */
        FUN_00e4e1fe(status_ret);

        /* Check for fallback conditions */
        if (*status_ret == file_$bad_reply_received_from_remote_node ||
            *status_ret == status_$naming_bad_directory) {
            /* Fall back to canned root handler for compatibility */
            DIR_$DIR_READU_FUN_00e4e1a8(status_ret);
        }
    }

    /* Check for error conditions that indicate no entries */
    if (*status_ret != status_$ok) {
        return;
    }

    /* Validate that we have entries or EOF */
    if (*eof_ret == 0 && *continuation == 0) {
        *status_ret = status_$naming_object_is_not_an_acl_object;
    }
}

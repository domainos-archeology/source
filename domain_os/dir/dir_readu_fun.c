/*
 * DIR_$DIR_READU_FUN_00e4e1a8 - Internal directory read helper
 *
 * Nested Pascal subprocedure of DIR_$DIR_READU. Handles canned root
 * directory reads and delegates to DIR_$OLD_DIR_READU for legacy reads.
 *
 * Original address: 0x00E4E1A8
 * Original size: 86 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$DIR_READU_FUN_00e4e1a8 - Internal directory read helper
 *
 * Originally a nested Pascal subprocedure that accessed its parent's
 * stack frame. Flattened to take explicit parameters from the parent
 * DIR_$DIR_READU function.
 *
 * If the directory is the canned replicated root, calls FUN_00e4dffe
 * for the special root read. Otherwise delegates to DIR_$OLD_DIR_READU.
 *
 * Parameters:
 *   dir_uid      - UID of directory to read (from parent frame)
 *   continuation - In/out: continuation position (from parent frame)
 *   max_entries  - Pointer to max entries to return (from parent frame)
 *   count_ret    - Pointer to count of entries returned (from parent frame)
 *   flags        - Read flags (from parent frame)
 *   eof_ret      - Pointer to EOF indicator (from parent frame)
 *   status_ret   - Output: status code
 */
void DIR_$DIR_READU_FUN_00e4e1a8(uid_t *dir_uid, int32_t *continuation,
                                  uint16_t *max_entries, int32_t *count_ret,
                                  void *flags, int32_t *eof_ret,
                                  status_$t *status_ret)
{
    /* Clear EOF indicator */
    *eof_ret = 0;

    /* Check if this is the canned replicated root */
    if (dir_uid->high == NAME_$CANNED_REP_ROOT_UID.high &&
        dir_uid->low == NAME_$CANNED_REP_ROOT_UID.low) {
        /* Use canned root read handler */
        FUN_00e4dffe();
    } else {
        /* Delegate to legacy directory read */
        DIR_$OLD_DIR_READU(dir_uid, continuation, max_entries,
                           count_ret, flags, eof_ret, status_ret);
    }
}

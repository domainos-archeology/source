/*
 * DIR_$ADDU - Add a directory entry
 *
 * Adds a new entry to a directory by delegating to the shared
 * add implementation with flags=0.
 *
 * Original address: 0x00E501A0
 * Original size: 50 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$ADDU - Add a directory entry
 *
 * This function is a thin wrapper around FUN_00e500b8 that adds
 * a simple directory entry (not a root entry with special flags).
 *
 * Parameters:
 *   dir_uid    - UID of parent directory
 *   name       - Name for the new entry
 *   name_len   - Pointer to name length (max 255)
 *   file_uid   - UID of file to add
 *   status_ret - Output: status code
 */
void DIR_$ADDU(uid_t *dir_uid, char *name, int16_t *name_len,
               uid_t *file_uid, status_$t *status_ret)
{
    /* Call shared implementation with flags=0 (no special root flags) */
    DIR_$ADD_ENTRY_INTERNAL(dir_uid, name, *name_len, file_uid, 0, status_ret);
}

/*
 * DIR_$OLD_ADDU - Legacy add directory entry
 *
 * Thin wrapper around FUN_00e5674c with hard_link_flag=0.
 *
 * Original address: 0x00E5694A
 * Original size: 48 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_ADDU - Legacy add directory entry
 *
 * Calls the shared add entry helper with hard_link_flag=0
 * to indicate a normal (non-hard-link) add.
 *
 * Parameters:
 *   dir_uid    - UID of parent directory
 *   name       - Name for the new entry
 *   name_len   - Pointer to name length
 *   file_uid   - UID of file to add
 *   status_ret - Output: status code
 */
void DIR_$OLD_ADDU(uid_t *dir_uid, char *name, int16_t *name_len,
                   uid_t *file_uid, status_$t *status_ret)
{
    FUN_00e5674c(dir_uid, name, (uint16_t)*name_len, file_uid, 0, status_ret);
}

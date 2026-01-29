/*
 * DIR_$OLD_ADD_HARD_LINKU - Legacy add hard link
 *
 * Thin wrapper around FUN_00e5674c with hard_link_flag=0xFF.
 *
 * Original address: 0x00E5697A
 * Original size: 48 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_ADD_HARD_LINKU - Legacy add hard link
 *
 * Calls the shared add entry helper with hard_link_flag=0xFF
 * to indicate a hard link operation.
 *
 * Parameters:
 *   dir_uid    - UID of parent directory
 *   name       - Name for the link
 *   name_len   - Pointer to name length
 *   target_uid - UID of target file
 *   status_ret - Output: status code
 */
void DIR_$OLD_ADD_HARD_LINKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                             uid_t *target_uid, status_$t *status_ret)
{
    FUN_00e5674c(dir_uid, name, *name_len, target_uid, 0xFF, status_ret);
}

/*
 * DIR_$SET_DAD - Set directory parent pointer
 *
 * Sets the parent directory pointer for a directory entry.
 * This is a thin wrapper around FILE_$SET_DIRPTR.
 *
 * Original address: 0x00E52B66
 * Original size: 38 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$SET_DAD - Set directory parent pointer
 *
 * This function simply delegates to FILE_$SET_DIRPTR to set the
 * parent directory pointer (DAD = Directory-Address-of-Directory).
 *
 * Parameters:
 *   dir_uid    - UID of directory whose parent to set
 *   parent_uid - UID of parent directory
 *   status_ret - Output: status code
 */
void DIR_$SET_DAD(uid_t *dir_uid, uid_t *parent_uid, status_$t *status_ret)
{
    FILE_$SET_DIRPTR(dir_uid, parent_uid, status_ret);
}

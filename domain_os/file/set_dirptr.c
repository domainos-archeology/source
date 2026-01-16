/*
 * FILE_$SET_DIRPTR - Set file directory pointer
 *
 * Simple wrapper around FILE_$SET_ATTRIBUTE with attr_id=5.
 *
 * Original address: 0x00E5E1B2
 */

#include "file/file_internal.h"

/*
 * FILE_$SET_DIRPTR
 *
 * Sets the parent directory pointer of a file.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   dir_uid    - UID of parent directory
 *   status_ret - Receives operation status
 *
 * The flags value 0xFFFF means:
 *   - Low 16 bits (0xFFFF): All rights required
 *   - High 16 bits (0x0000): No option flags
 */
void FILE_$SET_DIRPTR(uid_t *file_uid, uid_t *dir_uid, status_$t *status_ret)
{
    uid_t dir_copy;

    /* Copy the directory UID to local storage */
    dir_copy.high = dir_uid->high;
    dir_copy.low = dir_uid->low;

    /* Set attribute 5 (directory pointer) with flags 0xFFFF */
    FILE_$SET_ATTRIBUTE(file_uid, FILE_ATTR_DIR_PTR, &dir_copy,
                        0x0000FFFF, status_ret);
}

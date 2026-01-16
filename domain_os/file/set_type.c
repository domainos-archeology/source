/*
 * FILE_$SET_TYPE - Set file type UID
 *
 * Simple wrapper around FILE_$SET_ATTRIBUTE with attr_id=4.
 *
 * Original address: 0x00E5E176
 */

#include "file/file_internal.h"

/*
 * FILE_$SET_TYPE
 *
 * Sets the type UID of a file.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   type_uid   - New type UID to set
 *   status_ret - Receives operation status
 *
 * The flags value 0x20000 means:
 *   - Low 16 bits (0x0000): No specific rights required
 *   - High 16 bits (0x0002): Option flags
 */
void FILE_$SET_TYPE(uid_t *file_uid, uid_t *type_uid, status_$t *status_ret)
{
    uid_t type_copy;

    /* Copy the type UID to local storage */
    type_copy.high = type_uid->high;
    type_copy.low = type_uid->low;

    /* Set attribute 4 (type UID) with flags 0x20000 */
    FILE_$SET_ATTRIBUTE(file_uid, FILE_ATTR_TYPE_UID, &type_copy,
                        0x00020000, status_ret);
}

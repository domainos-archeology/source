/*
 * FILE_$SET_MGR_ATTR - Set manager attribute
 *
 * Wrapper around FILE_$SET_ATTRIBUTE with attr_id=14 (0x0E).
 *
 * Original address: 0x00E5E39A
 */

#include "file/file_internal.h"

/*
 * FILE_$SET_MGR_ATTR
 *
 * Sets the manager attribute of a file. The manager attribute is an
 * 8-byte value used by object managers.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   mgr_attr   - Pointer to manager attribute (8 bytes / 2 longs)
 *   version    - Pointer to version number (must be 0)
 *   status_ret - Receives operation status
 *
 * The flags value 0x2FFFF means:
 *   - Low 16 bits (0xFFFF): All rights required
 *   - High 16 bits (0x0002): Option flag bit 1
 */
void FILE_$SET_MGR_ATTR(uid_t *file_uid, void *mgr_attr, int16_t *version,
                        status_$t *status_ret)
{
    uint32_t value[2];

    /* Version must be 0 */
    if (*version != 0) {
        *status_ret = file_$invalid_arg;
        return;
    }

    /* Copy the 8-byte manager attribute to local storage */
    value[0] = ((uint32_t *)mgr_attr)[0];
    value[1] = ((uint32_t *)mgr_attr)[1];

    /* Set attribute 14 (manager attribute) with flags 0x2FFFF */
    FILE_$SET_ATTRIBUTE(file_uid, FILE_ATTR_MGR_ATTR, value,
                        0x0002FFFF, status_ret);
}

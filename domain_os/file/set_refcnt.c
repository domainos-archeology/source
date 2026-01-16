/*
 * FILE_$SET_REFCNT - Set reference count
 *
 * Wrapper around FILE_$SET_ATTRIBUTE with attr_id=8.
 * If reference count becomes 0, the file is deleted.
 *
 * Original address: 0x00E5E3F4
 */

#include "file/file_internal.h"

/*
 * FILE_$SET_REFCNT
 *
 * Sets the reference count of a file. Special handling:
 *   - 0xFFFFFFFF maps to 0 (release)
 *   - >= 0xFFF5 maps to -11 (0xFFF5)
 *   - If count becomes 0 and operation succeeds, file is deleted
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   refcnt     - Pointer to new reference count
 *   status_ret - Receives operation status
 *
 * The flags value 0x8FFFF means:
 *   - Low 16 bits (0xFFFF): All rights required
 *   - High 16 bits (0x0008): Option flag bit 3
 */
void FILE_$SET_REFCNT(uid_t *file_uid, uint32_t *refcnt, status_$t *status_ret)
{
    uint32_t count;
    int16_t ref_value;
    uint8_t result_buf[6];
    status_$t delete_status;

    count = *refcnt;

    /* Map special values */
    if (count == 0xFFFFFFFF) {
        /* -1 means release (set to 0) */
        ref_value = 0;
    } else if (count < 0xFFF5) {
        /* Normal range */
        ref_value = (int16_t)count;
    } else {
        /* Too large, set to -11 (0xFFF5) */
        ref_value = -11;
    }

    /* Set attribute 8 (reference count) with flags 0x8FFFF */
    FILE_$SET_ATTRIBUTE(file_uid, FILE_ATTR_REFCNT, &ref_value,
                        0x0008FFFF, status_ret);

    /* If refcount is now 0 and operation succeeded, delete the file */
    if (*status_ret == status_$ok && ref_value == 0) {
        /* Call delete internal with flags=1 (do the delete) */
        FILE_$DELETE_INT(file_uid, 1, result_buf, &delete_status);
        /* Note: delete status is discarded */
    }
}

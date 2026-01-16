/*
 * FILE_$SET_DEVNO - Set device number
 *
 * Simple wrapper around FILE_$SET_ATTRIBUTE with attr_id=22 (0x16).
 *
 * Original address: 0x00E5E362
 */

#include "file/file_internal.h"

/*
 * FILE_$SET_DEVNO
 *
 * Sets the device number attribute of a file.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   devno      - Pointer to device number (uint16_t)
 *   status_ret - Receives operation status
 *
 * The flags value 0x8FFFF means:
 *   - Low 16 bits (0xFFFF): All rights required
 *   - High 16 bits (0x0008): Option flag bit 3
 */
void FILE_$SET_DEVNO(uid_t *file_uid, uint16_t *devno, status_$t *status_ret)
{
    uint16_t devno_copy;

    /* Copy the device number to local storage */
    devno_copy = *devno;

    /* Set attribute 22 (device number) with flags 0x8FFFF */
    FILE_$SET_ATTRIBUTE(file_uid, FILE_ATTR_DEVNO, &devno_copy,
                        0x0008FFFF, status_ret);
}

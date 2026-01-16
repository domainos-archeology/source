/*
 * FILE_$ATTRIBUTES - Get file attributes (old format)
 *
 * Returns file attributes converted to old format via VTOCE_$NEW_TO_OLD.
 * Uses attribute flags 0x21.
 *
 * Original address: 0x00E5DA4C
 */

#include "file/file_internal.h"

/*
 * FILE_$ATTRIBUTES
 *
 * Gets file attributes and converts them to old format for backward
 * compatibility. Uses AST_$GET_ATTRIBUTES with flags 0x21.
 *
 * Parameters:
 *   file_uid   - UID of file
 *   attr_out   - Output buffer for old-format attributes
 *   status_ret - Receives operation status
 *
 * The internal UID lookup uses the file_uid to set up the lookup context
 * by storing the UID at local offset -0x20 and clearing bit 6 of the
 * flags byte.
 */
void FILE_$ATTRIBUTES(uid_t *file_uid, void *attr_out, status_$t *status_ret)
{
    status_$t status;
    uid_t local_uid;
    uint8_t new_attrs[144];  /* 0x90 bytes - full attribute buffer */
    uid_t returned_uid;

    /* Copy the file UID to local storage */
    local_uid.high = file_uid->high;
    local_uid.low = file_uid->low;

    /*
     * The original code stores the UID in a special location and clears
     * bit 6 of a flags byte. This is part of the Pascal-to-C nested
     * procedure mechanism where the inner function (AST_$GET_ATTRIBUTES)
     * accesses the outer function's local variables.
     *
     * For the C implementation, we just pass the UID directly.
     */

    /* Get attributes using flags 0x21 */
    AST_$GET_ATTRIBUTES(&returned_uid, 0x21, new_attrs, &status);

    if (status == status_$ok) {
        /* Convert new format to old format */
        VTOCE_$NEW_TO_OLD(new_attrs, NULL, attr_out);
    }

    *status_ret = status;
}

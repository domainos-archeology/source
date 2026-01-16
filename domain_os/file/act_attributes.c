/*
 * FILE_$ACT_ATTRIBUTES - Get active file attributes (old format)
 *
 * Like FILE_$ATTRIBUTES but uses flags 0x01 (locked access).
 * Returns attributes converted to old format.
 *
 * Original address: 0x00E5DAB0
 */

#include "file/file_internal.h"

/*
 * FILE_$ACT_ATTRIBUTES
 *
 * Gets active (currently open) file attributes and converts them to old
 * format for backward compatibility. Uses AST_$GET_ATTRIBUTES with
 * flags 0x01 which indicates locked/active access.
 *
 * Parameters:
 *   file_uid   - UID of file
 *   attr_out   - Output buffer for old-format attributes
 *   status_ret - Receives operation status
 */
void FILE_$ACT_ATTRIBUTES(uid_t *file_uid, void *attr_out, status_$t *status_ret)
{
    status_$t status;
    uid_t local_uid;
    uint8_t new_attrs[144];  /* 0x90 bytes - full attribute buffer */
    uid_t returned_uid;

    /* Copy the file UID to local storage */
    local_uid.high = file_uid->high;
    local_uid.low = file_uid->low;

    /* Get attributes using flags 0x01 (locked/active access) */
    AST_$GET_ATTRIBUTES(&returned_uid, 0x01, new_attrs, &status);

    if (status == status_$ok) {
        /* Convert new format to old format */
        VTOCE_$NEW_TO_OLD(new_attrs, NULL, attr_out);
    }

    *status_ret = status;
}

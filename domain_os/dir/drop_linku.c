/*
 * DIR_$DROP_LINKU - Drop a soft link
 *
 * Removes a symbolic link from a directory.
 *
 * Original address: 0x00E517F6
 * Original size: 198 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$DROP_LINKU - Drop a soft link
 *
 * Removes a symbolic link entry from a directory. Returns the UID
 * that was associated with the link (if any).
 *
 * Parameters:
 *   dir_uid    - UID of parent directory
 *   name       - Name of link to drop
 *   name_len   - Pointer to name length
 *   target_uid - Output: UID associated with link
 *   status_ret - Output: status code
 */
void DIR_$DROP_LINKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                     uid_t *target_uid, status_$t *status_ret)
{
    struct {
        uint8_t   op;
        uint8_t   padding[3];
        uid_t     uid;          /* Directory UID */
        uint16_t  reserved;
        uint8_t   gap[0x80];
        uint16_t  path_len;     /* Name length */
        char      name_data[255];
    } request;
    Dir_$OpResponse response;
    /* A6-relative 2-byte cell passed as DIR_$DO_OP's fifth argument;
     * it is REM_FILE_$SEND_REQUEST's `received_len` out-parameter
     * (source-32ld). */
    uint16_t do_op_rcvd_len;
    status_$t status;
    uint16_t len;
    int16_t i;

    /* Get name length */
    len = *name_len;

    /* Validate name length */
    if (len == 0 || len > DIR_MAX_LEAF_LEN) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* Copy name into request buffer */
    request.path_len = len;
    for (i = 0; i < len; i++) {
        request.name_data[i] = name[i];
    }

    /* Build the request */
    request.op = DIR_OP_DROP_LINKU;
    request.uid.high = dir_uid->high;
    request.uid.low = dir_uid->low;
    request.reserved = DAT_00e7fc9a;

    /* Send the request - size includes name length */
    DIR_$DO_OP(&request.op, len + DAT_00e7fc9e, 0x1c, &response, &do_op_rcvd_len);
    status = response.status;

    /* Check for fallback conditions */
    if (status == file_$bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* Fall back to old implementation */
        DIR_$OLD_DROP_LINKU(dir_uid, name, name_len, target_uid, status_ret);
    } else {
        /* 0xE518A4: lea (-0xc,A6),A0 - the returned UID is the 8 bytes at
         * response offset 0x14, not the 0x16 variant READ_LINKU uses. */
        target_uid->high = response.uid.high;
        target_uid->low = response.uid.low;
        *status_ret = status;
    }
}

/*
 * DIR_$ADD_HARD_LINKU - Add a hard link
 *
 * Creates a hard link to an existing file.
 *
 * Original address: 0x00E505CA
 * Original size: 196 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$ADD_HARD_LINKU - Add a hard link
 *
 * Creates a hard link entry in a directory pointing to an existing file.
 * The target file's reference count is incremented.
 *
 * Parameters:
 *   dir_uid    - UID of directory for link
 *   name       - Name for the link
 *   name_len   - Pointer to name length (max 255)
 *   target_uid - UID of target file
 *   status_ret - Output: status code
 */
void DIR_$ADD_HARD_LINKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                         uid_t *target_uid, status_$t *status_ret)
{
    struct {
        uint8_t   op;
        uint8_t   padding[3];
        uid_t     uid;          /* Directory UID */
        uint16_t  reserved;
        uint8_t   gap[0x80];
        uid_t     target;       /* Target file UID */
        uint16_t  path_len;     /* Name length */
        char      name_data[255];
    } request;
    Dir_$OpResponse response;
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
    request.op = DIR_OP_ADD_HARD_LINKU;
    request.uid.high = dir_uid->high;
    request.uid.low = dir_uid->low;
    request.reserved = DAT_00e7fc4a;
    request.target.high = target_uid->high;
    request.target.low = target_uid->low;

    /* Send the request - size includes name length */
    DIR_$DO_OP(&request.op, len + DAT_00e7fc4e, 0x14, &response, &request);
    status = response.status;

    /* Check for fallback conditions */
    if (status == status_$file_bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* Fall back to old implementation */
        DIR_$OLD_ADD_HARD_LINKU(dir_uid, name, name_len, target_uid, status_ret);
    } else {
        *status_ret = status;
    }
}

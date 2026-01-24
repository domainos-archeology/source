/*
 * DIR_$CREATE_DIRU - Create a subdirectory
 *
 * Creates a new directory as a child of the given directory.
 *
 * Original address: 0x00E529EE
 * Original size: 198 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$CREATE_DIRU - Create a subdirectory
 *
 * Creates a new directory entry and returns the UID of the
 * newly created directory.
 *
 * Parameters:
 *   parent_uid  - UID of parent directory
 *   name        - Name for new directory
 *   name_len    - Pointer to name length
 *   new_dir_uid - Output: UID of created directory
 *   status_ret  - Output: status code
 */
void DIR_$CREATE_DIRU(uid_t *parent_uid, char *name, uint16_t *name_len,
                      uid_t *new_dir_uid, status_$t *status_ret)
{
    struct {
        uint8_t   op;
        uint8_t   padding[3];
        uid_t     uid;          /* Parent directory UID */
        uint16_t  reserved;
        uint8_t   gap[0x80];
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
    request.op = DIR_OP_CREATE_DIRU;
    request.uid.high = parent_uid->high;
    request.uid.low = parent_uid->low;
    request.reserved = DAT_00e7fc7a;

    /* Send the request - size includes name length */
    DIR_$DO_OP(&request.op, len + DAT_00e7fc7e, 0x1c, &response, &request);
    status = response.status;

    /* Check for fallback conditions */
    if (status == file_$bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* Fall back to old implementation */
        DIR_$OLD_CREATE_DIRU(parent_uid, name, name_len, new_dir_uid, status_ret);
    } else {
        /* Store status and extract created UID from response */
        *status_ret = status;
        new_dir_uid->high = response._22_4_;
        new_dir_uid->low = response._24_4_;
    }
}

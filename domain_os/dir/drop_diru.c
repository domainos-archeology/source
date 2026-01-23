/*
 * DIR_$DROP_DIRU - Drop/delete a directory
 *
 * Removes a directory entry from its parent.
 *
 * Original address: 0x00E52AB4
 * Original size: 178 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$DROP_DIRU - Drop/delete a directory
 *
 * Removes a directory entry from its parent directory. The directory
 * being dropped must be empty.
 *
 * Parameters:
 *   parent_uid - UID of parent directory
 *   name       - Name of directory to drop
 *   name_len   - Pointer to name length
 *   status_ret - Output: status code
 */
void DIR_$DROP_DIRU(uid_t *parent_uid, char *name, uint16_t *name_len,
                    status_$t *status_ret)
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
    request.op = DIR_OP_DROP_DIRU;
    request.uid.high = parent_uid->high;
    request.uid.low = parent_uid->low;
    request.reserved = DAT_00e7fc82;

    /* Send the request - size includes name length */
    DIR_$DO_OP(&request.op, len + DAT_00e7fc86, 0x14, &response, &request);
    status = response.status;

    /* Check for fallback conditions */
    if (status == status_$file_bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* Fall back to old implementation
         * Note: OLD_DROP_DIRU has different parameter layout */
        DIR_$OLD_DROP_DIRU(parent_uid, name,
                          (uint16_t *)((uint32_t)name_len >> 16),
                          (uint16_t *)name_len, status_ret);
    } else {
        *status_ret = status;
    }
}

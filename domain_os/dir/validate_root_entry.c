/*
 * DIR_$VALIDATE_ROOT_ENTRY - Validate a root directory entry
 *
 * Verifies that an entry exists in the root directory.
 *
 * Original address: 0x00E5039A
 * Original size: 176 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$VALIDATE_ROOT_ENTRY - Validate a root directory entry
 *
 * Validates that the given name exists in the root directory.
 * Uses NAME_$ROOT_UID as the directory to search.
 *
 * Parameters:
 *   name       - Name to validate
 *   name_len   - Pointer to name length (max 255)
 *   status_ret - Output: status code
 */
void DIR_$VALIDATE_ROOT_ENTRY(char *name, uint16_t *name_len,
                              status_$t *status_ret)
{
    struct {
        uint8_t   op;
        uint8_t   padding[3];
        uid_t     root_uid;
        uint16_t  reserved;
        uint8_t   gap[0x88];
        uint16_t  len;
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
    request.len = len;
    for (i = 0; i < len; i++) {
        request.name_data[i] = name[i];
    }

    /* Build the request */
    request.op = DIR_OP_VALIDATE_ROOT_ENTRY;
    request.root_uid.high = NAME_$ROOT_UID.high;
    request.root_uid.low = NAME_$ROOT_UID.low;
    request.reserved = DAT_00e7fcda;

    /* Send the request - size includes name length */
    DIR_$DO_OP(&request.op, len + DAT_00e7fcde, 0x14, &response, &request);
    status = response.status;

    /* Check for fallback conditions */
    if (status == status_$file_bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* Fall back to old implementation */
        DIR_$OLD_VALIDATE_ROOT_ENTRY(name, name_len, status_ret);
    } else {
        *status_ret = status;
    }
}

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
    dir_$do_op_request_t request;
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
    request.body.name.path_len = len;
    for (i = 0; i < len; i++) {
        request.body.name.name[i] = name[i];
    }

    /* Build the request */
    /* 0x00E503E0-0x00E503F4.  The subject uid is the CONSTANT
     * NAME_$ROOT_UID (`movea.l #0xe8029c,A0` at 0x00E503E6), not a caller
     * argument. */
    request.op = DIR_OP_VALIDATE_ROOT_ENTRY;
    request.uid.high = NAME_$ROOT_UID.high;
    request.uid.low = NAME_$ROOT_UID.low;
    request.version = DIR_$OP_REC(DIR_OP_VALIDATE_ROOT_ENTRY >> 1).version;

    /* Send the request - size includes name length */
    DIR_$DO_OP(&request,
               (int16_t)(DIR_$OP_REC(DIR_OP_VALIDATE_ROOT_ENTRY >> 1).base_size + len),
               0x14, &response, &do_op_rcvd_len);
    status = response.status;

    /* Check for fallback conditions */
    if (status == file_$bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* Fall back to old implementation */
        DIR_$OLD_VALIDATE_ROOT_ENTRY(name, name_len, status_ret);
    } else {
        *status_ret = status;
    }
}

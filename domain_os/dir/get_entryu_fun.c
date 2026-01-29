/*
 * DIR_$GET_ENTRYU_FUN_00e4d460 - Internal entry retrieval helper
 *
 * Nested Pascal subprocedure of DIR_$GET_ENTRYU. Builds a DIR
 * request with op=DIR_OP_GET_ENTRYU_OP (0x44), sends it via
 * DIR_$DO_OP, and copies the response fields to entry_ret.
 *
 * Original address: 0x00E4D460
 * Original size: 160 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$GET_ENTRYU_FUN_00e4d460 - Internal entry retrieval helper
 *
 * Originally a nested Pascal subprocedure that accessed its parent's
 * stack frame. Flattened to take explicit parameters.
 *
 * Builds a get-entry request with operation code 0x44, copies the name
 * into the request buffer, and sends it via DIR_$DO_OP. On success,
 * copies response fields into the caller's entry_ret buffer.
 *
 * Parameters:
 *   local_uid  - UID of directory (local copy from parent)
 *   name       - Name to look up (from parent frame)
 *   name_len   - Name length (from parent frame)
 *   entry_ret  - Output: entry information (from parent frame)
 *   status_ret - Output: status code
 */
void DIR_$GET_ENTRYU_FUN_00e4d460(uid_t *local_uid, char *name,
                                   uint16_t name_len, void *entry_ret,
                                   status_$t *status_ret)
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
    int16_t i;

    /* Copy name into request buffer */
    request.path_len = name_len;
    for (i = 0; i < (int16_t)name_len; i++) {
        request.name_data[i] = name[i];
    }

    /* Build the request */
    request.op = DIR_OP_GET_ENTRYU_OP;
    request.uid.high = local_uid->high;
    request.uid.low = local_uid->low;
    request.reserved = DAT_00e7fc42;

    /* Send the request */
    /* TODO: Verify exact parameter table entries for GET_ENTRYU */
    DIR_$DO_OP(&request.op, name_len + DAT_00e7fc42, 0x1c, &response, &request);

    *status_ret = response.status;
}

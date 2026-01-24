/*
 * DIR_$FIX_DIR - Fix/repair a directory
 *
 * Attempts to repair a corrupted directory structure by sending
 * a FIX_DIR operation to the directory server.
 *
 * Original address: 0x00E53E84
 * Original size: 116 bytes
 */

#include "dir/dir_internal.h"

/*
 * Request structure for FIX_DIR operation
 */
typedef struct {
    uint8_t   op;           /* Operation code: DIR_OP_FIX_DIR (0x48) */
    uint8_t   padding[3];
    uid_t     uid;          /* Directory UID to fix */
    uint16_t  reserved;     /* Reserved field */
} Dir_$FixDirRequest;

/*
 * DIR_$FIX_DIR - Fix/repair a directory
 *
 * Sends a FIX_DIR request to the directory server. If the server returns
 * an error indicating it doesn't support the new protocol, falls back
 * to the old DIR_$OLD_FIX_DIR implementation.
 *
 * Parameters:
 *   dir_uid    - UID of directory to fix
 *   status_ret - Output: status code
 */
void DIR_$FIX_DIR(uid_t *dir_uid, status_$t *status_ret)
{
    Dir_$FixDirRequest request;
    Dir_$OpResponse response;

    /* Build the request */
    request.op = DIR_OP_FIX_DIR;
    request.uid.high = dir_uid->high;
    request.uid.low = dir_uid->low;
    request.reserved = DAT_00e7fcba;

    /* Send the request */
    DIR_$DO_OP(&request.op, DAT_00e7fcbe, 0x14, &response, &request);

    /* Check for fallback conditions */
    if (response.status == file_$bad_reply_received_from_remote_node ||
        response.status == status_$naming_bad_directory) {
        /* Fall back to old implementation */
        DIR_$OLD_FIX_DIR(dir_uid, status_ret);
    } else {
        *status_ret = response.status;
    }
}

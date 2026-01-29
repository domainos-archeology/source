/*
 * DIR_$SET_ACL - Set ACL on a directory entry
 *
 * Builds a DIR_OP_SET_ACL request and sends it via DO_OP.
 * Falls back to FILE_$PRIV_LOCK + FILE_$SET_ACL + FILE_$PRIV_UNLOCK
 * if the new protocol fails.
 *
 * Original address: 0x00E52C86
 * Original size: 234 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$SET_ACL - Set ACL on a directory entry
 *
 * Attempts the SET_ACL operation via the new protocol. If the server
 * does not support it, falls back to the old FILE_$PRIV_LOCK /
 * FILE_$SET_ACL / FILE_$PRIV_UNLOCK sequence.
 *
 * Parameters:
 *   uid        - UID of the object
 *   acl        - ACL data to apply
 *   status_ret - Output: status code
 */
void DIR_$SET_ACL(uid_t *uid, void *acl, status_$t *status_ret)
{
    struct {
        uint8_t   op;
        uint8_t   padding[3];
        uid_t     dir_uid;
        uint16_t  reserved;
        uint8_t   gap[0x80];
        uid_t     acl_uid;
    } request;
    Dir_$OpResponse response;
    status_$t status;
    uint32_t lock_handle;
    uint16_t lock_result;
    uint32_t dtv_buf[2];

    /* Build the request */
    request.op = DIR_OP_SET_ACL;
    request.dir_uid.high = uid->high;
    request.dir_uid.low = uid->low;
    request.reserved = DAT_00e7fcc2;
    request.acl_uid.high = ((uid_t *)acl)->high;
    request.acl_uid.low = ((uid_t *)acl)->low;

    /* Send the request */
    DIR_$DO_OP(&request.op, DAT_00e7fcc6, 0x14, &response, &request);
    status = response.status;

    /* Check for fallback conditions */
    if (status == file_$bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* Fall back to old FILE_$PRIV_LOCK / SET_ACL / PRIV_UNLOCK sequence */
        FILE_$PRIV_LOCK(uid, PROC1_$AS_ID, 0, 4, 0, 0x880000, 0, 0, 0,
                        &DAT_00e54730, 1, &lock_handle, &lock_result, status_ret);
        if (*status_ret == status_$ok) {
            FILE_$SET_ACL(uid, (uid_t *)acl, status_ret);
            FILE_$PRIV_UNLOCK(uid, (uint16_t)lock_handle, 0x00040000 | PROC1_$AS_ID,
                              0, 0, 0, dtv_buf, &status);
            if (*status_ret == status_$ok) {
                *status_ret = status;
            }
        }
    } else {
        *status_ret = status;
    }
}

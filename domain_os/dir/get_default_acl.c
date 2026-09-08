/*
 * DIR_$GET_DEFAULT_ACL - Read a directory's default ACL uid
 *
 * Original address: 0x00E531C8
 * Original size: 150 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$GET_DEFAULT_ACL (0x00E531C8)
 *
 * Builds a DIR_OP_GET_DEFAULT_ACL request and sends it through DIR_$DO_OP,
 * falling back to DIR_$OLD_GET_DEFAULT_ACL (0x00E564E6).
 *
 * Frame: `link.w A6,-0xbc` - request base A6-0xB8, reply A6-0x20
 * (0x1C bytes), received-length word A6-0xBA.
 *
 * Parameters (A6+0x08..A6+0x14):
 *   dir_uid    - UID of the directory
 *   acl_type   - ACL type uid; the whole request body, at +0x8E
 *   acl_ret    - out: the default ACL uid, from reply+0x14
 *   status_ret - out: status code
 */
void DIR_$GET_DEFAULT_ACL(uid_t *dir_uid, uid_t *acl_type, uid_t *acl_ret,
                          status_$t *status_ret)
{
    dir_$do_op_request_t request;
    Dir_$OpResponse response;
    /* A6-0xBA: DIR_$DO_OP's `received_len` out-parameter (source-32ld). */
    uint16_t do_op_rcvd_len;
    status_$t status;

    /* 0x00E531E6-0x00E531F6 */
    request.op = DIR_OP_GET_DEFAULT_ACL;
    request.uid.high = dir_uid->high;
    request.uid.low = dir_uid->low;
    request.version = DIR_$OP_REC(DIR_OP_GET_DEFAULT_ACL >> 1).version;
    /* 0x00E531FC: the ACL type uid is the whole body, at +0x8E. */
    request.body.uid_body.uid.high = acl_type->high;
    request.body.uid_body.uid.low = acl_type->low;

    /* 0x00E53206-0x00E5321A */
    DIR_$DO_OP(&request,
               (int16_t)DIR_$OP_REC(DIR_OP_GET_DEFAULT_ACL >> 1).base_size,
               0x1c, &response, &do_op_rcvd_len);

    /* 0x00E53222 */
    status = response.status;

    if (status == file_$bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* 0x00E53236-0x00E5323E */
        DIR_$OLD_GET_DEFAULT_ACL(dir_uid, acl_type, acl_ret, status_ret);
    } else {
        /* 0x00E53246: `lea (-0xc,A6),A0` with the reply based at A6-0x20 -
         * the uid comes from reply+0x14, not the +0x16 variant. */
        acl_ret->high = response.uid.high;
        acl_ret->low = response.uid.low;
        *status_ret = status;
    }
}

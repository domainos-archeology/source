/*
 * DIR_$SET_DEF_PROTECTION - Set default protection for a directory
 *
 * Sets the default ACL/protection that will be applied to new
 * entries created in this directory.
 *
 * Original address: 0x00E520A6
 * Original size: 196 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$SET_DEF_PROTECTION - Set default protection for a directory
 *
 * Sets the default protection settings for a directory. These
 * settings will be applied to new files created in the directory.
 *
 * Parameters:
 *   dir_uid    - UID of directory
 *   acl_type   - ACL type UID
 *   prot_buf   - Protection data to set (44 bytes, 11 uint32_t values)
 *   prot_uid   - Protection UID
 *   status_ret - Output: status code
 */
void DIR_$SET_DEF_PROTECTION(uid_t *dir_uid, uid_t *acl_type,
                             void *prot_buf, uid_t *prot_uid,
                             status_$t *status_ret)
{
    dir_$do_op_request_t request;
    Dir_$OpResponse response;
    /* A6-relative 2-byte cell passed as DIR_$DO_OP's fifth argument;
     * it is REM_FILE_$SEND_REQUEST's `received_len` out-parameter
     * (source-32ld). */
    uint16_t do_op_rcvd_len;
    status_$t status;
    uint32_t *src, *dst;
    int16_t i;

    /* Build the request */
    request.op = DIR_OP_SET_DEF_PROTECTION;
    request.uid.high = dir_uid->high;
    request.uid.low = dir_uid->low;
    request.version = DIR_$OP_REC(DIR_OP_SET_DEF_PROTECTION >> 1).version;
    /* 0x00E520DE: the ACL type uid at +0x8E. */
    request.body.set_def_prot.acl_type_uid.high = acl_type->high;
    request.body.set_def_prot.acl_type_uid.low = acl_type->low;

    /* Copy protection data into request */
    src = (uint32_t *)prot_buf;
    /* 0x00E520EA-0x00E520F2: `moveq #0xa,D0` + `dbf` = eleven longwords
     * (44 bytes) at +0x96. */
    dst = request.body.set_def_prot.prot;
    for (i = 0; i < 11; i++) {
        *dst++ = *src++;
    }

    /* Copy protection UID */
    /* 0x00E520F6: the ACL uid at +0xC2. */
    request.body.set_def_prot.acl_uid.high = prot_uid->high;
    request.body.set_def_prot.acl_uid.low = prot_uid->low;

    /* Send the request */
    DIR_$DO_OP(&request,
               (int16_t)DIR_$OP_REC(DIR_OP_SET_DEF_PROTECTION >> 1).base_size,
               0x14, &response, &do_op_rcvd_len);
    status = response.status;

    /* Check for fallback conditions */
    if (status == file_$bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* Convert to old 9ACL format and use old implementation */
        uid_t temp_acl;
        ACL_$CONVERT_TO_9ACL(prot_buf, prot_uid, dir_uid,
                             acl_type, &temp_acl, status_ret);
        if (*status_ret == status_$ok) {
            DIR_$OLD_SET_DEFAULT_ACL(dir_uid, acl_type, &temp_acl, status_ret);
        }
    } else {
        *status_ret = status;
    }
}

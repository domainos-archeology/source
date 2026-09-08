/*
 * DIR_$GET_DEF_PROTECTION - Read a directory's default protection block
 *
 * Original address: 0x00E51D54
 * Original size: 196 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$GET_DEF_PROTECTION (0x00E51D54)
 *
 * Builds a DIR_OP_GET_DEF_PROTECTION request and sends it through
 * DIR_$DO_OP, asking for a 0x48-byte reply: the 44-byte protection block
 * at reply+0x14 and a uid at reply+0x40.  On a "bad reply"/"bad directory"
 * reply it reads the old-format ACL with DIR_$OLD_GET_DEFAULT_ACL
 * (0x00E564E6) and renders it with ACL_$CONVERT_FROM_9ACL (0x00E48F56).
 *
 * Frame: `link.w A6,-0xec` - request base A6-0xE8, reply A6-0x50
 * (0x48 bytes), received-length word A6-0xEA.
 *
 * Parameters (A6+0x08..A6+0x18):
 *   dir_uid    - UID of the directory
 *   acl_type   - ACL type uid; the whole request body, at +0x8E
 *   prot_buf   - out: 11 longwords (44 bytes) of protection data
 *   prot_uid   - out: the ACL uid at reply+0x40
 *   status_ret - out: status code
 */
void DIR_$GET_DEF_PROTECTION(uid_t *dir_uid, uid_t *acl_type,
                             void *prot_buf, uid_t *prot_uid,
                             status_$t *status_ret)
{
    dir_$do_op_request_t request;
    Dir_$OpResponse response;
    /* A6-0xEA: DIR_$DO_OP's `received_len` out-parameter (source-32ld). */
    uint16_t do_op_rcvd_len;
    status_$t status;
    uid_t old_acl;              /* A6-0x08 */
    const uint32_t *src;
    uint32_t *dst;
    int16_t i;

    /* 0x00E51D76-0x00E51D86 */
    request.op = DIR_OP_GET_DEF_PROTECTION;
    request.uid.high = dir_uid->high;
    request.uid.low = dir_uid->low;
    request.version = DIR_$OP_REC(DIR_OP_GET_DEF_PROTECTION >> 1).version;
    /* 0x00E51D8C: the ACL type uid is the whole body, at +0x8E. */
    request.body.uid_body.uid.high = acl_type->high;
    request.body.uid_body.uid.low = acl_type->low;

    /* 0x00E51D96-0x00E51DAA */
    DIR_$DO_OP(&request,
               (int16_t)DIR_$OP_REC(DIR_OP_GET_DEF_PROTECTION >> 1).base_size,
               0x48, &response, &do_op_rcvd_len);

    /* 0x00E51DB2 */
    status = response.status;

    if (status == file_$bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* 0x00E51DC6-0x00E51DD0 */
        DIR_$OLD_GET_DEFAULT_ACL(dir_uid, acl_type, &old_acl, status_ret);
        /* 0x00E51DDA: `tst.l (A2)` */
        if (*status_ret == status_$ok) {
            /* 0x00E51DDE-0x00E51DEA */
            ACL_$CONVERT_FROM_9ACL(&old_acl, acl_type, prot_buf, prot_uid,
                                   status_ret);
        }
    } else {
        /* 0x00E51DF2-0x00E51DFC: `moveq #0xa,D1` + `dbf` copies ELEVEN
         * longwords from reply+0x14. */
        src = (const uint32_t *)(const void *)((const uint8_t *)&response + 0x14);
        dst = (uint32_t *)prot_buf;
        for (i = 0; i < 11; i++) {
            *dst++ = *src++;
        }
        /* 0x00E51E00: the uid at reply+0x40. */
        {
            const uint32_t *p =
                (const uint32_t *)(const void *)((const uint8_t *)&response + 0x40);
            prot_uid->high = p[0];
            prot_uid->low = p[1];
        }
        /* 0x00E51E0C */
        *status_ret = status;
    }
}

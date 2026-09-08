/*
 * DIR_$SET_DEFAULT_ACL - Set the default ACL of a directory
 *
 * Original address: 0x00E53004
 * Original size: 292 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$SET_DEFAULT_ACL (0x00E53004)
 *
 * Builds a DIR_OP_SET_DEFAULT_ACL request and sends it through DIR_$DO_OP.
 * On a "bad reply"/"bad directory" reply it normalises the ACL uid locally
 * and calls DIR_$OLD_SET_DEFAULT_ACL (0x00E561CC).
 *
 * Frame: `link.w A6,-0x10c`
 *   A6-0x10A  received-length word
 *   A6-0x108  local copy of the directory uid  (0x00E5301A)
 *   A6-0x100  local copy of the ACL uid        (0x00E53026)
 *   A6-0x0F8  request base
 *   A6-0x058  reply (0x14 bytes)
 *   A6-0x040  acl_$prot_data_t scratch block
 *   A6-0x010  normalised ACL uid
 *   A6-0x008  ACL type uid ACL_$CONVERT_FUNKY_ACL reports back
 *
 * The body is a bare pair of uids and needs no variable-length part, so the
 * request size is the table's base_size (0x10) on its own.
 *
 * Parameters (A6+0x08..A6+0x14):
 *   dir_uid    - UID of the directory
 *   acl_type   - ACL type uid; goes to request+0x8E
 *   acl_uid    - ACL uid to install; goes to request+0x96
 *   status_ret - out: status code
 */
void DIR_$SET_DEFAULT_ACL(uid_t *dir_uid, uid_t *acl_type, uid_t *acl_uid,
                          status_$t *status_ret)
{
    uid_t local_dir, local_acl;
    dir_$do_op_request_t request;
    Dir_$OpResponse response;
    /* A6-0x10A: DIR_$DO_OP's `received_len` out-parameter (source-32ld). */
    uint16_t do_op_rcvd_len;
    status_$t status;
    acl_$prot_data_t prot_data;   /* A6-0x40 */
    uid_t result_acl;             /* A6-0x10 */
    uid_t conv_type_uid;          /* A6-0x08 */

    /* 0x00E5301A / 0x00E53026 */
    local_dir.high = dir_uid->high;
    local_dir.low = dir_uid->low;
    local_acl.high = acl_uid->high;
    local_acl.low = acl_uid->low;

    /* 0x00E53032-0x00E53044 */
    request.op = DIR_OP_SET_DEFAULT_ACL;
    request.uid.high = local_dir.high;
    request.uid.low = local_dir.low;
    request.version = DIR_$OP_REC(DIR_OP_SET_DEFAULT_ACL >> 1).version;
    /* 0x00E5304A: the caller's acl_type at +0x8E ... */
    request.body.set_default_acl.acl_type_uid.high = acl_type->high;
    request.body.set_default_acl.acl_type_uid.low = acl_type->low;
    /* 0x00E53054: ... and the local copy of the ACL uid at +0x96. */
    request.body.set_default_acl.acl_uid.high = local_acl.high;
    request.body.set_default_acl.acl_uid.low = local_acl.low;

    /* 0x00E53060-0x00E53074 */
    DIR_$DO_OP(&request,
               (int16_t)DIR_$OP_REC(DIR_OP_SET_DEFAULT_ACL >> 1).base_size,
               0x14, &response, &do_op_rcvd_len);

    /* 0x00E5307C */
    status = response.status;

    if (status == file_$bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {

        /* 0x00E53092 */
        result_acl.high = local_acl.high;
        result_acl.low = local_acl.low;

        /* 0x00E5309E-0x00E530AC: `and.w #0xff0` on the HIGH word of
         * local_acl.low, `lsr.w #4`, `andi.w #0xe0` - i.e. bits 25..27 of
         * the low longword.  Non-zero selects the "funky" encodings. */
        if ((local_acl.low & 0x0E000000u) != 0) {
            /* 0x00E530AE-0x00E530C0 */
            ACL_$CONVERT_FUNKY_ACL(&local_acl, &prot_data, &result_acl,
                                   &conv_type_uid, status_ret);
            /* 0x00E530CA: `tst.l (A3)` */
            if (*status_ret != status_$ok) {
                return;
            }
        }

        /* 0x00E530CE-0x00E530DC: the same word extraction on result_acl,
         * then `btst.l #4` - bit 24 of the low longword. */
        if ((result_acl.low & 0x01000000u) != 0) {
            /* 0x00E530DE: `bclr.b #0x0,(-0xc,A6)` clears that same bit. */
            result_acl.low &= ~0x01000000u;
        } else {
            /* 0x00E530E6-0x00E530FA.  The scratch protection block is
             * argument 1 and the caller's acl_type argument 4; result_acl
             * is both the source and the destination uid. */
            ACL_$CONVERT_TO_9ACL(&prot_data, &result_acl, &local_dir,
                                 acl_type, &result_acl, status_ret);
            /* 0x00E53104 */
            if (*status_ret != status_$ok) {
                return;
            }
        }

        /* 0x00E53108-0x00E53114 */
        DIR_$OLD_SET_DEFAULT_ACL(&local_dir, acl_type, &result_acl, status_ret);
    } else {
        /* 0x00E5311C */
        *status_ret = status;
    }
}

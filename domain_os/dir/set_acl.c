/*
 * DIR_$SET_ACL - Set the ACL of an object named in a directory
 *
 * Original address: 0x00E52C86
 * Original size: 234 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$SET_ACL (0x00E52C86)
 *
 * Builds a DIR_OP_SET_ACL request and sends it through DIR_$DO_OP.  On a
 * "bad reply"/"bad directory" reply it falls back to the local sequence
 * FILE_$PRIV_LOCK / FILE_$SET_ACL / FILE_$PRIV_UNLOCK.
 *
 * Frame: `link.w A6,-0xc4` - request base A6-0xB0, reply A6-0x18
 * (0x14 bytes), received-length word A6-0xC4.  The body is a bare ACL uid
 * at +0x8E and needs no variable-length part, so the request size is the
 * table's base_size on its own.
 *
 * Parameters (A6+0x08..A6+0x10):
 *   uid        - UID of the object
 *   acl        - ACL uid to apply
 *   status_ret - out: status code
 */
void DIR_$SET_ACL(uid_t *uid, void *acl, status_$t *status_ret)
{
    dir_$do_op_request_t request;
    Dir_$OpResponse response;
    /* A6-0xC4: DIR_$DO_OP's `received_len` out-parameter (source-32ld). */
    uint16_t do_op_rcvd_len;
    status_$t status;         /* A6-0xB8, FILE_$PRIV_UNLOCK's status cell  */
    uint32_t lock_slot;       /* A6-0xB4, FILE_$PRIV_LOCK's slot_io        */
    uint16_t lock_rights;     /* A6-0xC2, FILE_$PRIV_LOCK's rights_out     */
    uint32_t dtv_out;         /* A6-0xC0, FILE_$PRIV_UNLOCK's dtv_out      */

    /* 0x00E52CA0-0x00E52CB0 */
    request.op = DIR_OP_SET_ACL;
    request.uid.high = uid->high;
    request.uid.low = uid->low;
    request.version = DIR_$OP_REC(DIR_OP_SET_ACL >> 1).version;
    /* 0x00E52CB6: the ACL uid is the whole body, at +0x8E. */
    request.body.uid_body.uid.high = ((uid_t *)acl)->high;
    request.body.uid_body.uid.low = ((uid_t *)acl)->low;

    /* 0x00E52CC0-0x00E52CD4 */
    DIR_$DO_OP(&request,
               (int16_t)DIR_$OP_REC(DIR_OP_SET_ACL >> 1).base_size,
               0x14, &response, &do_op_rcvd_len);

    /* 0x00E52CDC */
    status = response.status;

    if (status == file_$bad_reply_received_from_remote_node ||
        status == status_$naming_bad_directory) {
        /* 0x00E52CF0-0x00E52D20.  `move.l #0x40000` fills the lock_mode /
         * local_only word pair (4, FALSE) and `move.l #0x80000` the
         * flags / key pair (0x0008, 0).  `pea (-0x79c6,PC)` at 0x00E52D00
         * is the NIL longword at 0x00E4B33C = DIR_$CONST_ZERO_L, whose
         * address is the acl_ctx argument.
         *
         * The `subq.l #0x2,SP` at 0x00E52CF0 reserves a two-byte Pascal
         * function-result slot that the `lea (0x30,SP),SP` at 0x00E52D26
         * discards; every DIR call site of FILE_$PRIV_LOCK does the same
         * (e.g. 0x00E514F6 in dir_$do_op_delete).  The tree declares
         * FILE_$PRIV_LOCK as a procedure, so nothing is collected here.
         * TODO: source-8g8b - decide whether FILE_$PRIV_LOCK is a Pascal
         * function whose word result every caller drops. */
        FILE_$PRIV_LOCK(uid, PROC1_$AS_ID, 0, 4, 0, 0x0008, 0x0000, 0, 0, 0,
                        (void **)&DIR_$CONST_ZERO_L, 1,
                        &lock_slot, &lock_rights, status_ret);

        /* 0x00E52D2A: `tst.l (A4)` - the whole status longword. */
        if (*status_ret == status_$ok) {
            /* 0x00E52D2E-0x00E52D34 */
            FILE_$SET_ACL(uid, (uid_t *)acl, status_ret);

            /* 0x00E52D3E-0x00E52D5C.  The unlock reports into its own
             * status cell (A6-0xB8) and 0x00E52D62 branches straight to
             * the epilogue: the image discards both that status and the
             * routine's boolean result. */
            (void)FILE_$PRIV_UNLOCK(uid, (int32_t)lock_slot, 4,
                                    (uint16_t)PROC1_$AS_ID,
                                    0, 0, 0, 0, &dtv_out, &status);
        }
    } else {
        /* 0x00E52D64 */
        *status_ret = status;
    }
}

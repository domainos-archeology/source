/*
 * dir_$do_op_set_acl - DIR_$DO_OP server-side handler for the set-ACL opcode
 *
 * Server-side handler for opcode 0x4A (DIR_OP_SET_ACL) in DIR_$DO_OP.  It is
 * the server half of the dir_$do_op_XXX / DIR_$XXX pairing used throughout
 * this module; the client-side request builder is DIR_$SET_ACL (0x00E52C86,
 * dir/set_acl.c), which is a DIFFERENT function.
 *
 * Sole caller: 0x00E4C78E, inside DIR_$DO_OP (0x00E4C02C), which passes
 *   uid        = the resolved object UID (DIR_$DO_OP's A6-0x10 local)
 *   acl_uid    = request + 0x8E, the "funky" ACL UID from the wire request
 *   status_ret = response + 0x04
 *
 * Flow (0x00E52BC2 .. 0x00E52C84):
 *   1. ACL_$ENTER_SUPER
 *   2. dir_$open_dir(uid, mode 2, rights 0, &handle, status_ret)
 *   3. If open succeeded, test the funky-ACL selector bits; zero means the
 *      encoding is not implemented -> status_$acl_unimplemented_call.
 *   4. Otherwise ACL_$CONVERT_FUNKY_ACL then, on success, FILE_$SET_PROT
 *      with protection type 4.
 *   5. dir_$release_handle, ACL_$EXIT_SUPER.
 *   6. Unconditionally (i.e. even when open_dir or the conversion failed)
 *      audit the operation when auditing is enabled.
 *
 * Note the super-mode bracket here spans the FILE_$SET_PROT call, unlike
 * dir_$do_op_set_prot (0x00E5216A) which drops out of super mode across it.
 *
 * Original address: 0x00E52BC2
 * Original size: 196 bytes
 */

#include "dir/dir_internal.h"

/* (23001c) "attempt to issue unimplemented ACL call" */
#define status_$acl_unimplemented_call  0x0023001CUL

/*
 * The word 4 handed to FILE_$SET_PROT as its protection type.
 *
 * Original address: 0x00E515BA, a constant word 0x0004 sitting in the
 * inter-function literal area after dir_$do_op_delete's `unlk`/`rts`.  It is
 * reached here by `pea (-0x1680,PC)` at 0x00E52C38 and is shared with
 * dir_$do_op_delete (0x00E51594) and dir_$write_def_prot (0x00E51F0C).
 */
static const uint16_t DIR_PROT_TYPE_4 = 4;

void dir_$do_op_set_acl(uid_t *uid, uid_t *acl_uid, status_$t *status_ret)
{
    uint32_t handle;            /* A6-0x4C */
    uint32_t acl_data[12];      /* A6-0x48: 48 bytes of converted ACL data */
    uid_t    prot_info;         /* A6-0x18 */
    uid_t    target_uid;        /* A6-0x10 (the frame's last 8 bytes are unused) */
    uint16_t funky_bits;

    ACL_$ENTER_SUPER();

    /* 0x00E52BDC: mode and rights arrive as one `move.l #0x20000` push,
     * i.e. mode = 2 (write), rights = 0. */
    dir_$open_dir(uid, 2, 0, &handle, status_ret);

    if (*status_ret == status_$ok) {
        /*
         * 0x00E52BF6-0x00E52C04: `move.w #0xff0,D0; and.w (0x4,A3),D0;
         * lsr.w #4,D0; andi.w #0xe0,D0`.  (0x4,A3) is the HIGH word of the
         * funky UID's low longword, so read it with a shift rather than a
         * byte/word pointer cast.
         */
        funky_bits = (uint16_t)(((acl_uid->low >> 16) & 0x0FF0) >> 4) & 0x00E0;

        if (funky_bits == 0) {
            *status_ret = status_$acl_unimplemented_call;
        } else {
            ACL_$CONVERT_FUNKY_ACL(acl_uid, acl_data, &prot_info, &target_uid,
                                   status_ret);
            if (*status_ret == status_$ok) {
                FILE_$SET_PROT(uid, (uint16_t *)&DIR_PROT_TYPE_4,
                               acl_data, &prot_info, status_ret);
            }
        }
    }

    dir_$release_handle(&handle);
    ACL_$EXIT_SUPER();

    /*
     * 0x00E52C58: AUDIT_$ENABLED is a Domain BOOLEAN tested `tst.b` + `bpl`,
     * a SIGNED test.  The audit record names ACL_$DIRIN_ACL (0xE1745C) as the
     * ACL type and the converted prot_info as the subject; prot_flags = 4.
     */
    if ((int8_t)AUDIT_$ENABLED < 0) {
        audit_$log_prot_op(*status_ret, uid, acl_data, &ACL_$DIRIN_ACL,
                           &prot_info, 4);
    }
}

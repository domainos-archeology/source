/*
 * dir_$do_op_set_default_acl - Server-side handler for SET_DEFAULT_ACL op
 *
 * Server-side handler for opcode 0x4C in DIR_$DO_OP. Sets the default
 * ACL for a directory. Enters supervisor mode, opens the directory with
 * write access, delegates to dir_$set_default_acl_internal for the actual ACL
 * conversion and storage, then cleans up.
 *
 * Called by DIR_$DO_OP case 0x4C.
 *
 * Argument mapping (call site 0x00E4C794, pushed right to left):
 *   pea (0x4,A3)   -> status_ret   (response + 0x04)
 *   pea (0x96,A2)  -> param_3      (request + 0x96)
 *   pea (0x8e,A2)  -> param_2      (request + 0x8E)
 *   pea (-0x10,A6) -> param_1      (resolved directory UID)
 * so param_2 IS request+0x8E and param_3 IS request+0x96.  Both are handed
 * to dir_$set_default_acl_internal in that order (0x00E52FDC pushes param_2,
 * 0x00E52FD8 pushes param_3).  In that callee param_2 lands in D5 and is
 * compared against ACL_$DIR_ACL (0x00E1744C, at 0x00E52E64) and ACL_$FILE_ACL
 * (0x00E17444, at 0x00E52EB4), so request+0x8E is an 8-byte ACL TYPE UID;
 * param_3 lands in A2, whose "funky" bits are tested at 0x00E52DA0
 * (and.w (0x4,A2),D0w) before ACL_$CONVERT_TO_10ACL / ACL_$CONVERT_FUNKY_ACL,
 * so request+0x96 is the 8-byte source ACL UID.
 *
 * Parameters:
 *   dir_uid      - UID of the directory
 *   acl_type     - ACL type UID: ACL_$DIR_ACL or ACL_$FILE_ACL (request + 0x8E)
 *   src_acl_uid  - Source ACL object UID, possibly in "funky" form
 *                  (request + 0x96)
 *   status_ret   - Output: status code
 *
 * Original address: 0x00E52FA6
 * Original size: 94 bytes
 */

#include "dir/dir_internal.h"

void dir_$do_op_set_default_acl(uid_t *dir_uid, void *acl_type, void *src_acl_uid,
                                 status_$t *status_ret)
{
    uint32_t handle;
    uint32_t pad;   /* local_c[1] - unused but part of stack frame */

    ACL_$ENTER_SUPER();

    /* Open directory with write access (mode=2) and rights=8 */
    dir_$open_dir(dir_uid, 2, 8, &handle, status_ret);

    if (*status_ret == status_$ok) {
        /* Delegate to the ACL-setting helper; flush_flag = 0xFF (true) */
        dir_$set_default_acl_internal(handle, acl_type, src_acl_uid, (char)0xFF,
                                      status_ret);
    }

    /* Release directory handle */
    dir_$release_handle(&handle);

    ACL_$EXIT_SUPER();
}

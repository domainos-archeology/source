/*
 * dir_$do_op_set_default_acl - Server-side handler for SET_DEFAULT_ACL op
 *
 * Server-side handler for opcode 0x4C in DIR_$DO_OP. Sets the default
 * ACL for a directory. Enters supervisor mode, opens the directory with
 * write access, delegates to dir_$set_default_acl_internal for the actual ACL conversion
 * and storage, then cleans up.
 *
 * Called by DIR_$DO_OP case 0x4C.
 *
 * Parameters:
 *   dir_uid    - UID of the directory
 *   acl_data   - ACL type/data from request (req + 0x96)
 *   acl_param  - ACL parameter from request (req + 0x8e)
 *   status_ret - Output: status code
 *
 * Original address: 0x00E52FA6
 * Original size: 94 bytes
 */

#include "dir/dir_internal.h"

void dir_$do_op_set_default_acl(uid_t *dir_uid, void *acl_data, void *acl_param,
                                 status_$t *status_ret)
{
    uint32_t handle;
    uint32_t pad;   /* local_c[1] - unused but part of stack frame */

    ACL_$ENTER_SUPER();

    /* Open directory with write access (mode=2) and rights=8 */
    dir_$open_dir(dir_uid, 2, 8, &handle, status_ret);

    if (*status_ret == status_$ok) {
        /* Delegate to the ACL-setting helper with all_entries flag (0xFF) */
        dir_$set_default_acl_internal(handle, acl_data, acl_param, (char)0xFF, status_ret);
    }

    /* Release directory handle */
    dir_$release_handle(&handle);

    ACL_$EXIT_SUPER();
}

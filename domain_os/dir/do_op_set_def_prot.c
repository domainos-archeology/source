/*
 * dir_$do_op_set_def_prot - DO_OP handler for set default protection
 *
 * Server-side handler for opcode 0x54 (DIR_OP_SET_DEF_PROTECTION) in
 * DIR_$DO_OP. Opens the directory for write (mode 2, rights 8 = write ACL),
 * writes the default protection data via dir_$write_def_prot, then releases
 * the handle.
 *
 * Parameters:
 *   uid        - Directory UID
 *   acl_type   - ACL type UID (DIR_ACL or FILE_ACL)
 *   prot_buf   - Protection data to set (44 bytes)
 *   prot_uid   - Protection UID
 *   status_ret - Output: status code
 *
 * Original address: 0x00E52044
 * Original size: 98 bytes
 */

#include "dir/dir_internal.h"

/* dir_$write_def_prot - Write default protection to directory page 0
 *
 * Converts funky ACL format if needed, validates the ACL UID is on the
 * same volume, checks set-ACL permissions, copies protection data to
 * the appropriate offset in page 0, updates the ACL UID, increments
 * the new ACL's refcount, truncates the old ACL, and optionally writes
 * the page via FILE_$FW_PARTIAL.
 *
 * Original address: 0x00E51E18
 */
extern void dir_$write_def_prot(uint32_t handle, void *acl_type,
                                void *prot_buf, void *prot_uid,
                                char flush_flag, status_$t *status_ret);

void dir_$do_op_set_def_prot(uid_t *uid, void *acl_type, void *prot_buf,
                             void *prot_uid, status_$t *status_ret)
{
    uint32_t local_handle;

    ACL_$ENTER_SUPER();

    /* Open directory for write with ACL right 8 (write ACL) */
    dir_$open_dir(uid, 2, 8, &local_handle, status_ret);

    if (*status_ret == status_$ok) {
        /* Write default protection; flush_flag = 0xFF (true) */
        dir_$write_def_prot(local_handle, acl_type, prot_buf, prot_uid,
                            (char)0xFF, status_ret);
    }

    dir_$release_handle(&local_handle);
    ACL_$EXIT_SUPER();
}

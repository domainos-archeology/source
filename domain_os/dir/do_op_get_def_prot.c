/*
 * dir_$do_op_get_def_prot - DO_OP handler for get default protection
 *
 * Server-side handler for opcode 0x56 (DIR_OP_GET_DEF_PROTECTION) in
 * DIR_$DO_OP. Opens the directory for read (mode 1, rights 0), reads
 * the default protection data via dir_$read_def_prot, then releases
 * the handle.
 *
 * Parameters:
 *   uid        - Directory UID
 *   acl_type   - ACL type UID (DIR_ACL or FILE_ACL)
 *   prot_buf   - Output: protection data (44 bytes)
 *   prot_uid   - Output: protection UID
 *   status_ret - Output: status code
 *
 * Original address: 0x00E51CF6
 * Original size: 94 bytes
 */

#include "dir/dir_internal.h"

/* dir_$read_def_prot - Read default protection from directory page 0
 *
 * Maps page 0, compares the ACL type against ACL_$DIR_ACL and ACL_$FILE_ACL,
 * copies 44 bytes of protection data from the appropriate offset (0x1A for
 * DIR, 0x4E for FILE), and copies the ACL UID from offset 0x46 or 0x7A.
 *
 * Original address: 0x00E51C6A
 */
extern void dir_$read_def_prot(uint32_t handle, void *acl_type,
                               void *prot_buf, uid_t *prot_uid,
                               status_$t *status_ret);

void dir_$do_op_get_def_prot(uid_t *uid, void *acl_type, void *prot_buf,
                             uid_t *prot_uid, status_$t *status_ret)
{
    uint32_t local_handle;

    ACL_$ENTER_SUPER();

    /* Open directory for read (mode 1, rights 0) */
    dir_$open_dir(uid, 1, 0, &local_handle, status_ret);

    if (*status_ret == status_$ok) {
        dir_$read_def_prot(local_handle, acl_type, prot_buf, prot_uid, status_ret);
    }

    dir_$release_handle(&local_handle);
    ACL_$EXIT_SUPER();
}

/*
 * dir_$add_bak_default_prot - Add backup entry with default protection
 *
 * Originally a nested Pascal subprocedure of dir_$do_op_add_bak. This
 * function handles the case where the original entry name is NOT found
 * in the directory. It reads the default FILE ACL protection, applies
 * it to the backup UID, adds a new directory entry (type 2 = file),
 * clears the rollback flag, and writes the file.
 *
 * Parameters:
 *   local_handle  - Directory handle (parent's local variable)
 *   uid           - Directory UID (parent's 1st param)
 *   name_ptr      - Entry name (parent's 3rd param)
 *   name_len      - Length of name (parent's 4th param)
 *   backup_uid    - UID of backup file (parent's 5th param)
 *   status_ret    - Output: status code (parent's 7th param)
 *   rollback_flag - Parent's local rollback flag (cleared on success)
 *
 * Original address: 0x00E50790
 * Original size: 160 bytes
 */

#include "dir/dir_internal.h"

/* DAT_00e50830 - Protection type parameter for FILE_$SET_PROT */

void dir_$add_bak_default_prot(uint32_t local_handle, uid_t *uid,
                                void *name_ptr, uint16_t name_len,
                                uid_t *backup_uid, status_$t *status_ret,
                                char *rollback_flag)
{
    uint32_t prot_buf[12];
    uid_t acl_uid[2];

    /* Read default FILE ACL protection from directory page 0 */
    dir_$read_def_prot(local_handle, &ACL_$FILE_ACL, prot_buf, acl_uid, status_ret);
    if (*status_ret != status_$ok) {
        return;
    }

    /* Apply protection to the backup file UID */
    FILE_$SET_PROT(backup_uid, &DAT_00e50830, prot_buf, acl_uid, status_ret);
    if (*status_ret != status_$ok) {
        return;
    }

    /* Add directory entry: type 2 (file), extra=0, link_len=0,
     * link_data=dir_$find_entry (passed as callback placeholder) */
    dir_$add_entry(local_handle, name_ptr, name_len, 2, 0,
                   backup_uid, 0, dir_$find_entry, status_ret);
    if (*status_ret != status_$ok) {
        return;
    }

    /* Success - clear rollback flag and write the file */
    *rollback_flag = 0;
    FILE_$FW_FILE(uid, status_ret);
}

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

/*
 * 0x00E50830, the word 0x0005 sitting immediately after this routine's own
 * `rts` at 0x00E5082E.  Image bytes: 00 05.  It is FILE_$SET_PROT's
 * prot_type VAR argument (`move.w (A4),D2w` at 0x00E5DF56), reached with
 * `pea (0x64,PC)` at 0x00E507CA - the only reference to the cell.
 * (Ghidra labelled the cell by its address, 0x00E50830.)  source-p25p.
 */
static const uint16_t dir_$add_bak_prot_type_00e50830 = 5;

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
    /* 0x00E507BE-0x00E507D2 */
    FILE_$SET_PROT(backup_uid,
                   (uint16_t *)&dir_$add_bak_prot_type_00e50830,
                   prot_buf, acl_uid, status_ret);
    if (*status_ret != status_$ok) {
        return;
    }

    /* 0x00E507E4-0x00E50806: type 2 (file), extra 0, link_len 0.  The
     * image's `pea (-0x3e08,PC)` at 0x00E507EA resolves to 0x00E4C9E4,
     * dir_$find_entry's own entry point - a dummy the compiler emitted for
     * the unused link_data pointer (link_len is 0, so it is never read). */
    dir_$add_entry(local_handle, name_ptr, name_len, 2, 0,
                   backup_uid, 0, dir_$find_entry, status_ret);
    if (*status_ret != status_$ok) {
        return;
    }

    /* Success - clear rollback flag and write the file */
    *rollback_flag = 0;
    FILE_$FW_FILE(uid, status_ret);
}

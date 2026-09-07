/*
 * dir_$do_op_add_bak - DO_OP handler for add backup entry
 *
 * Server-side handler for opcode 0x34 (DIR_OP_ADD_BAKU) in DIR_$DO_OP.
 * Adds a backup version of a file entry to a directory. The backup name
 * is constructed by appending a 4-character suffix (from A5+0x2120) to
 * the original name.
 *
 * Process:
 * 1. Initialize result_uid to UID_$NIL
 * 2. Compute backup name: original name + suffix, capped at 0x20 (type 0)
 *    or 0xFF (other types)
 * 3. Enter super mode, open directory for write
 * 4. Verify backup UID is on the same volume via AST_$GET_LOCATION
 * 5. Set attribute 6 (link count increment) on the backup UID
 * 6. Look up original entry by name:
 *    a. If not found: call dir_$add_bak_default_prot to read default
 *       FILE ACL protection, set protection on backup UID, add entry,
 *       and write file
 *    b. If found as type 4 (link): return invalid_link_operation
 *    c. If found: extract original UID, check ACL rights, copy ACL from
 *       original to backup, then look up the backup name:
 *       - If backup exists and is type 4: invalid_link_operation
 *       - If backup exists: check rights, verify on same volume, check
 *         object type is valid (not 0, 4, or 5), delete old backup via
 *         FILE_$DELETE_OBJ, update entry in-place with original UID
 *       - If backup doesn't exist: add new entry
 * 7. Re-find original entry (crash if not found), update its UID to backup UID
 * 8. Clear rollback flag, write file
 * 9. On cleanup: if rollback needed, undo attribute 6 (set attribute 7)
 *
 * Parameters:
 *   uid         - Directory UID
 *   type        - Entry type (0 for short names, non-zero for long names)
 *   name        - Entry name
 *   name_len    - Length of name
 *   backup_uid  - UID of backup file
 *   result_uid  - Output: UID of replaced backup (or UID_$NIL)
 *   status_ret  - Output: status code
 *
 * Original address: 0x00E50832
 * Original size: 1064 bytes
 */

#include "dir/dir_internal.h"

/* `move.w #0x81,-(SP)` at 0x00E50AFA - the AST_$GET_COMMON_ATTRIBUTES
 * selector this site uses. */
#define DIR_CATTR_ADD_BAK       0x0081

/* DAT_00e50c5c / DAT_00e50c5a - ACL rights parameters for add_bak */

/* DAT_00e50830 - Protection type parameter for FILE_$SET_PROT */

void dir_$do_op_add_bak(uid_t *uid, uint16_t type, void *name_ptr, uint16_t name_len,
                         void *uid_data, uid_t *result_uid, status_$t *status_ret)
{
    uid_t *backup_uid = (uid_t *)uid_data;
    char *a5 = (char *)__A5_BASE();
    int32_t bak_name_len;
    char rollback_flag;
    uint32_t local_handle;
    void *entry_ptr;
    uint16_t extra_buf1[2];
    uint16_t extra_buf2[2];
    uint8_t lookup_buf[4];
    status_$t local_status;
    /* A6-0x68 in the image: a full 0x20-byte object-location descriptor.
     * AST_$GET_ATTRIBUTES writes all 32 bytes back on success (0x00E049B0).
     * TODO(source-qgq): retype as file_$obj_loc_t and drop loc_uid_high /
     * loc_uid_low / loc_flags / loc_vol_id, which are its +0x08, +0x0C,
     * +0x1D and +0x02 fields. */
    uint8_t loc_desc[0x20];
    int16_t loc_vol_id;
    uint32_t loc_uid_high;
    uint32_t loc_uid_low;
    uint8_t loc_flags;
    uint8_t get_loc_buf1[4];
    uint8_t get_loc_buf2[4];
    uint8_t attr_buf[4];
    ast_$common_attr_t common_attrs;    /* A6-0x80, 0x18 bytes */
    uint8_t common_buf[40];
    char type_byte;
    uid_t orig_uid;
    uid_t old_bak_uid;
    uint8_t delete_buf[2];
    uint16_t attr_val[28];

    /* Buffer for backup name: up to 257 bytes (entry name + 4-char suffix) */
    uint8_t bak_name[258];
    char *name_chars = (char *)name_ptr;

    /* Initialize result UID to NIL */
    result_uid->high = UID_$NIL.high;
    result_uid->low = UID_$NIL.low;

    rollback_flag = 0;

    /* Compute maximum backup name length based on type */
    if (type == 0) {
        bak_name_len = (int32_t)name_len + 4;
        if (bak_name_len > 0x20) {
            bak_name_len = 0x20;
        }
    } else {
        bak_name_len = (int32_t)name_len + 4;
        if (bak_name_len > 0xFF) {
            bak_name_len = 0xFF;
        }
    }

    /* Copy original name into backup name buffer */
    {
        int16_t remaining = name_len - 1;
        if (remaining >= 0) {
            int16_t i = 1;
            do {
                bak_name[i] = name_chars[i - 1];
                i++;
                remaining--;
            } while (remaining != -1);
        }
    }

    /* Append 4-character backup suffix from A5+0x2120 */
    {
        int16_t remaining = 3;
        int16_t svar = (int16_t)bak_name_len;
        int16_t j = 1;
        do {
            bak_name[(int16_t)(svar - 4 + j)] = *(uint8_t *)(a5 + j + 0x211F);
            j++;
            remaining--;
        } while (remaining != -1);
    }

    ACL_$ENTER_SUPER();

    /* Open directory for write */
    dir_$open_dir(uid, 2, 0, &local_handle, status_ret);
    if (*status_ret != status_$ok) {
        goto done;
    }

    /* Verify backup UID is on the same volume */
    loc_uid_high = backup_uid->high;
    loc_uid_low = backup_uid->low;
    loc_flags &= 0xBF;  /* Clear bit 6 */
    AST_$GET_LOCATION(loc_desc, 1, get_loc_buf1, get_loc_buf2, &local_status);
    if (local_status != status_$ok ||
        *(int16_t *)((char *)(uintptr_t)local_handle + 0x3A) != loc_vol_id ||
        (int8_t)loc_flags < 0) {
        *status_ret = local_status;
        if (*status_ret == status_$ok) {
            *status_ret = file_$objects_on_different_volumes;
        }
        goto done;
    }

    /* Increment link count on backup UID (attribute 6, value 1) */
    attr_val[0] = 1;
    AST_$SET_ATTRIBUTE(backup_uid, 6, attr_val, &local_status);
    if (local_status != status_$ok) {
        *status_ret = local_status;
        goto done;
    }

    /* Mark rollback needed */
    rollback_flag = -1;  /* 0xFF = true */

    /* Look up the original entry by name */
    {
        char found;
        found = dir_$find_entry((void *)(uintptr_t)local_handle, name_ptr,
                                name_len, 0, &entry_ptr, extra_buf1, extra_buf2);
        if (found >= 0) {
            /* Original not found - use nested helper to set default
             * protection and add the backup entry directly */
            dir_$add_bak_default_prot(local_handle, uid, name_ptr, name_len,
                                       backup_uid, status_ret, &rollback_flag);
            goto done;
        }
    }

    {
        uint8_t *ep = (uint8_t *)entry_ptr;

        /* Reject link entries */
        if ((*ep & 7) == 4) {
            *status_ret = status_$naming_invalid_link_operation;
            goto done;
        }

        /* Extract original entry's UID */
        orig_uid.high = *(uint32_t *)(ep + 4);
        orig_uid.low = *(uint32_t *)(ep + 8);
        uint32_t orig_extra = *(uint32_t *)(ep + 0x0C);

        /* Check ACL rights on original */
        ACL_$RIGHTS(&orig_uid, &DAT_00e4bc24, &DAT_00e50c5c, &DAT_00e50c5a, status_ret);
        if (*status_ret != status_$ok) {
            if (*status_ret == status_$wrong_type) {
                goto not_a_file;
            }
            NAME_CONVERT_ACL_STATUS(status_ret);
            goto done;
        }

        /* Copy ACL from original to backup */
        if (orig_uid.high == backup_uid->high && orig_uid.low == backup_uid->low) {
            /* Same UID - no copy needed */
            goto done;
        }
        ACL_$COPY(&orig_uid, backup_uid, &ACL_$FILEIN_ACL, &ACL_$FILEIN_ACL, status_ret);
        if (*status_ret != status_$ok) {
            goto done;
        }

        /* Look up the backup name */
        {
            char found2;
            uint8_t *bak_ep;

            found2 = dir_$find_entry((void *)(uintptr_t)local_handle,
                                     bak_name + 1, (int16_t)bak_name_len, 0,
                                     &entry_ptr, extra_buf1, extra_buf2);
            bak_ep = (uint8_t *)entry_ptr;

            if (found2 < 0) {
                /* Backup name exists */
                if ((*bak_ep & 7) == 4) {
                    *status_ret = status_$naming_invalid_link_operation;
                    goto done;
                }

                /* Extract old backup UID */
                old_bak_uid.high = *(uint32_t *)(bak_ep + 4);
                old_bak_uid.low = *(uint32_t *)(bak_ep + 8);

                /* Check ACL rights on old backup */
                ACL_$RIGHTS(&old_bak_uid, &DAT_00e4bc24, &DAT_00e50c5c, &DAT_00e50c5a, status_ret);
                if (*status_ret != status_$ok) {
                    /* Clear high bit and check for wrong_type */
                    *status_ret &= 0x7FFFFFFF;
                    if (*status_ret != status_$wrong_type) {
                        if (*status_ret == status_$wrong_type) {
                            goto not_a_file;
                        }
                        NAME_CONVERT_ACL_STATUS(status_ret);
                        goto done;
                    }
                }

                /* Verify old backup is on same volume and is a valid type */
                loc_uid_high = old_bak_uid.high;
                loc_uid_low = old_bak_uid.low;
                loc_flags &= 0xBF;
                AST_$GET_COMMON_ATTRIBUTES((uid_t *)(void *)loc_desc,
                                           DIR_CATTR_ADD_BAK, &common_attrs,
                                           &local_status);  /* 0x00E50AFE */

                if ((int8_t)loc_flags >= 0 && local_status == status_$ok &&
                    *(int16_t *)((char *)(uintptr_t)local_handle + 0x3A) == loc_vol_id) {

                    /*
                     * 0x00E50B24-0x00E50B3C: `move.b (-0x7f,A6),D0b` with the
                     * record at A6-0x80 reads sub_type, then three `seq`s are
                     * OR'ed and `bmi` takes the delete path when ANY of them
                     * matched.  Only sub-types 0, 4 and 5 may be replaced; any
                     * other sub-type is 0x000E0010 (0x00E50B42).
                     */
                    type_byte = (char)common_attrs.sub_type;
                    if (type_byte != 0 && type_byte != 4 && type_byte != 5) {
                        goto not_a_file;
                    }

                    /* Delete old backup object */
                    FILE_$DELETE_OBJ(&old_bak_uid, 0xFF, delete_buf, status_ret);
                    if (*status_ret != status_$ok) {
                        goto done;
                    }
                    if ((int8_t)delete_buf[0] < 0) {
                        result_uid->high = old_bak_uid.high;
                        result_uid->low = old_bak_uid.low;
                    }
                }

                /* Update existing backup entry in-place with original UID */
                *(uint32_t *)(bak_ep + 4) = orig_uid.high;
                *(uint32_t *)(bak_ep + 8) = orig_uid.low;
                *(uint32_t *)(bak_ep + 0x0C) = orig_extra;
            } else {
                /* Backup name doesn't exist - add new entry */
                dir_$add_entry(local_handle, bak_name + 1, (int16_t)bak_name_len,
                               2, 0, &orig_uid, 0, dir_$find_entry, status_ret);
                if (*status_ret != status_$ok) {
                    goto done;
                }
            }
        }

        /* Re-find original entry and update with backup UID */
        {
            char found3;
            found3 = dir_$find_entry((void *)(uintptr_t)local_handle, name_ptr,
                                     name_len, 0, &entry_ptr, extra_buf1, extra_buf2);
            if (found3 >= 0) {
                CRASH_SYSTEM(&Naming_bad_request_header_ver_err);
            }
            uint8_t *ep2 = (uint8_t *)entry_ptr;
            *(uint32_t *)(ep2 + 4) = backup_uid->high;
            *(uint32_t *)(ep2 + 8) = backup_uid->low;
        }

        rollback_flag = 0;
        FILE_$FW_FILE(uid, status_ret);
        goto done;
    }

not_a_file:
    *status_ret = status_$naming_branch_is_not_a_directory;

done:
    dir_$release_handle(&local_handle);
    ACL_$EXIT_SUPER();

    /* If an error occurred and we had incremented the link count, undo it */
    if (*status_ret != status_$ok && (int8_t)rollback_flag < 0) {
        AST_$SET_ATTRIBUTE(backup_uid, 7, attr_val, &local_status);
    }
}

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

/*
 * Constant cells for the two ACL_$RIGHTS calls (0x00E509EA and 0x00E50AA6),
 * addressed with `pea (d,PC)` (PC = instruction address + 2).  Both calls
 * pass the same three cells.
 */

/* 0x00E4BC24, byte 0xFF: ACL_$RIGHTS' ignore_super argument (TRUE - the
 * super-user bypass is suppressed).  `pea (-0x4dc0,PC)` at 0x00E509E2 and
 * `pea (-0x4e7c,PC)` at 0x00E50A9E. */

/* 0x00E50C5C, longword 0x00000002: the required rights mask.
 * `pea (0x27c,PC)` at 0x00E509DE and `pea (0x1c0,PC)` at 0x00E50A9A. */
static const uint32_t dir_$add_bak_rights_00e50c5c = 0x00000002;

/* 0x00E50C5A, word 0x0000: ACL_$RIGHTS' option flags.
 * `pea (0x27e,PC)` at 0x00E509DA and `pea (0x1c2,PC)` at 0x00E50A96. */
static const int16_t dir_$add_bak_acl_opts_00e50c5a = 0;

/* 0x00E50830 (word 0x0005, FILE_$SET_PROT's prot_type) is read only by
 * dir_$add_bak_default_prot and is a file static there (source-p25p). */

void dir_$do_op_add_bak(uid_t *uid, uint16_t type, void *name_ptr, uint16_t name_len,
                         void *uid_data, uid_t *result_uid, status_$t *status_ret)
{
    uid_t *backup_uid = (uid_t *)uid_data;
    int32_t bak_name_len;
    char rollback_flag;
    uint32_t local_handle;
    void *entry_ptr;
    uint16_t extra_buf1[2];
    uint16_t extra_buf2[2];
    uint8_t lookup_buf[4];
    status_$t local_status;
    /* A6-0x68 in the image: the 0x20-byte object-location descriptor.
     * AST_$GET_ATTRIBUTES writes all 32 bytes back on success (0x00E049B0).
     * loc_vol_id is the WORD at +0x02, i.e. file_$obj_loc_t.volume. */
    file_$obj_loc_t loc_desc;
#define loc_vol_id  ((int16_t)loc_desc.volume)
    uint32_t get_loc_buf1;   /* 0x00e50906 pea (-0x184,A6) - never touched */
    uint32_t get_loc_buf2;   /* 0x00e50902 pea (-0x18c,A6) - aote+0x08 out */
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
            /* 0x00E508B4 `lea (0x0,A5,D1w),A0; move.b (0x211f,A0),...` with a 1-based D1: the ".bak"
             * arm declared from its bias byte. */
            bak_name[(int16_t)(svar - 4 + j)] =
                (uint8_t)DIR_$DATA.bak_char[j];
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
    loc_desc.uid.high = backup_uid->high;
    loc_desc.uid.low = backup_uid->low;
    loc_desc.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;  /* Clear bit 6 */
    AST_$GET_LOCATION(&loc_desc, 1, &get_loc_buf1, &get_loc_buf2,
                      &local_status);
    if (local_status != status_$ok ||
        *(int16_t *)((char *)(uintptr_t)local_handle + 0x3A) != loc_vol_id ||
        loc_desc.flags < 0) {
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
        ACL_$RIGHTS(&orig_uid,
                    (boolean *)&DIR_$CONST_TRUE_B,
                    (uint32_t *)&dir_$add_bak_rights_00e50c5c,
                    (int16_t *)&dir_$add_bak_acl_opts_00e50c5a, status_ret);
        /* 0x00E509F8-0x00E50A06: `cmpi.l #0x230004,(A1)` - "wrong type -
         * operation illegal on system objects" takes the not-a-file path;
         * every other failure is converted and returned. */
        if (*status_ret != status_$ok) {
            if (*status_ret == status_$acl_wrong_type) {
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
                ACL_$RIGHTS(&old_bak_uid,
                            (boolean *)&DIR_$CONST_TRUE_B,
                            (uint32_t *)&dir_$add_bak_rights_00e50c5c,
                            (int16_t *)&dir_$add_bak_acl_opts_00e50c5a,
                            status_ret);
                if (*status_ret != status_$ok) {
                    /* 0x00E50AB8: `bclr.b #0x7,(A1)` clears bit 31 of the
                     * status longword. */
                    *status_ret &= 0x7FFFFFFF;
                    /* 0x00E50AC0: only "object not found" carries on; every
                     * other failure re-enters the SAME decision the first
                     * ACL_$RIGHTS uses (`bra.w 0x00e509fc` at 0x00E50AC8). */
                    if (*status_ret != status_$file_object_not_found) {
                        if (*status_ret == status_$acl_wrong_type) {
                            goto not_a_file;
                        }
                        NAME_CONVERT_ACL_STATUS(status_ret);
                        goto done;
                    }
                }

                /* Verify old backup is on same volume and is a valid type */
                loc_desc.uid.high = old_bak_uid.high;
                loc_desc.uid.low = old_bak_uid.low;
                loc_desc.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;
                AST_$GET_COMMON_ATTRIBUTES(&loc_desc,
                                           DIR_CATTR_ADD_BAK, &common_attrs,
                                           &local_status);  /* 0x00E50AFE */

                if (loc_desc.flags >= 0 && local_status == status_$ok &&
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

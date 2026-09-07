/*
 * DIR_$OLD_ADD_BAKU - Legacy add backup entry
 *
 * Creates a backup entry by renaming the existing file to .bak
 * and adding the new file with the original name.
 *
 * Original address: 0x00E56E3E
 * Original size: 812 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_ADD_BAKU - Legacy add backup entry
 *
 * Based on the Ghidra decompilation at 0x00E56E3E:
 * 1. Validate leaf name via name_$validate_leaf
 * 2. Compute backup name (name + ".bak", max 32 chars)
 * 3. Enter super mode / acquire directory lock
 * 4. Look up existing entry via dir_$old_find_entry
 * 5. If not found:
 *    a. Unlock, exit super
 *    b. Get default ACL, set protection on new file
 *    c. Add hard link for the new file
 *    d. Flush file
 * 6. If found (entry type 1):
 *    a. Check rights on existing file
 *    b. Build .BAK name for the mapped name
 *    c. Check if .BAK entry already exists
 *    d. If exists, check rights and drop it
 *    e. Unlock, get attributes, set protection
 *    f. Rename old entry to .bak, add new with original name
 *    g. Flush file
 *
 * Parameters:
 *   dir_uid    - UID of directory
 *   name       - Name for the backup entry
 *   name_len   - Pointer to name length
 *   backup_uid - UID of the new backup file
 *   status_ret - Output: status code
 */
/*
 * Constant cells for the two ACL_$RIGHTS calls (0x00E56FD2 and 0x00E5705A),
 * addressed with `pea (d,PC)` (PC = instruction address + 2).  Both calls
 * pass the same three cells.
 */

/* 0x00E5716C, byte 0xFF: ACL_$RIGHTS' ignore_super argument (TRUE - the
 * super-user bypass is suppressed).  `pea (0x1a0,PC)` at 0x00E56FCA and
 * `pea (0x118,PC)` at 0x00E57052. */
static const boolean dir_$old_add_baku_ignore_super_00e5716c = true;

/* 0x00E56946, longword 0x00000002: the required rights mask.
 * `pea (-0x682,PC)` at 0x00E56FC6 and `pea (-0x70a,PC)` at 0x00E5704E. */
static const uint32_t dir_$old_add_baku_rights_00e56946 = 0x00000002;

/* 0x00E5472E, word 0x0000: ACL_$RIGHTS' option flags.  This is the shared
 * literal zero word NAME_$CONST_ZERO_W.  `pea (-0x2896,PC)` at 0x00E56FC2
 * and `pea (-0x291e,PC)` at 0x00E5704A. */
static const int16_t dir_$old_add_baku_acl_opts_00e5472e = 0;

void DIR_$OLD_ADD_BAKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                       uid_t *backup_uid, status_$t *status_ret)
{
    uint8_t parsed_name[32];
    uint16_t parsed_len;
    char name_buf[36];         /* local_110: 4 extra bytes for ".bak" */
    uint32_t handle;
    int32_t entry;
    int32_t bak_entry;
    uint16_t param5, param6;
    int8_t valid;
    int8_t found;
    int8_t bak_found;
    int16_t bak_name_len;
    uid_t old_file_uid;
    uid_t entry_uid;
    uid_t default_acl;
    uid_t prot_uid;
    /* A6-0x38: the 11-longword ACL data record FILE_$SET_PROT forwards
     * (FILE_$OLD_AP copies it with `moveq #0xa; move.l (A1)+,(A3)+`
     * at 0x00E5E13C). */
    uint32_t acl_data[12];
    /* A6-0x58: FILE_$GET_ATTRIBUTES' 0x20-byte location record
     * (`pea (-0x58,A6)` at 0x00E5709C). */
    file_$obj_loc_t attr_buf;
    /* A6-0xE8: its 0x90-byte attribute buffer (`pea (-0xe8,A6)` at
     * 0x00E57098); the callee requires size_ptr == 0x90. */
    uint8_t attr_buf2[AST_ATTR_REC_SIZE];
    uint32_t attr_data[16];
    uint8_t result_buf[8];
    status_$t local_status;
    int16_t name_offset;
    int16_t i;

    /* Step 1: Validate leaf name */
    valid = name_$validate_leaf(name, *name_len, parsed_name, &parsed_len);
    if (valid >= 0 || ((int16_t)parsed_len > 0x1c && parsed_len != *name_len)) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* Step 2: Compute backup name length */
    if ((int16_t)*name_len < 0x1d) {
        bak_name_len = *name_len + 4;
    } else {
        bak_name_len = 0x20;  /* Max 32 chars */
    }

    /* Copy original name into buffer (32 bytes) */
    for (i = 0; i < 32; i++) {
        name_buf[i + 4] = name[i < (int16_t)*name_len ? i : 0];
    }
    /* Note: name_buf+4 is the actual start of the name data */

    /* Append ".bak" at the computed offset */
    name_buf[bak_name_len] = '.';
    name_buf[bak_name_len + 1] = 'b';
    name_buf[bak_name_len + 2] = 'a';
    name_buf[bak_name_len + 3] = 'k';

    /* Step 3: Enter super mode / acquire directory lock */
    NAME_$LOCK_DIR(dir_uid, &handle, 4, 0, status_ret);
    if ((int16_t)*status_ret != 0) {
        ACL_$EXIT_SUPER();
        return;
    }

    /* Step 4: Look up existing entry */
    found = dir_$old_find_entry(handle, parsed_name, parsed_len,
                         &entry, &param5, &param6);

    if (found >= 0) {
        /* Step 5: Entry not found - simple add path */
        NAME_$UNLOCK_DIR(status_ret);
        ACL_$EXIT_SUPER();
        if ((int16_t)*status_ret != 0) {
            return;
        }

        /* Get default ACL for files */
        ACL_$DEF_ACLDATA(acl_data, &default_acl);
        DIR_$OLD_GET_DEFAULT_ACL(dir_uid, &ACL_$FILE_ACL, &default_acl, status_ret);
        if ((int16_t)*status_ret != 0) {
            return;
        }

        /* Set protection on the new file */
        FILE_$SET_PROT(backup_uid, &DAT_00e5716a, acl_data, &default_acl, status_ret);
        if ((int16_t)*status_ret != 0) {
            return;
        }

        /* Add hard link for the new file */
        DIR_$OLD_ADD_HARD_LINKU(dir_uid, name, name_len, backup_uid, status_ret);
        if ((int16_t)*status_ret != 0) {
            return;
        }

        /* Flush */
        FILE_$FW_FILE(dir_uid, status_ret);
        return;
    }

    /* Step 6: Entry found - check type */
    if (*((uint8_t *)(entry + 0x27)) != 0x01) {
        /* Not a regular file entry */
        *status_ret = status_$naming_invalid_link_operation;
        NAME_$UNLOCK_DIR(&local_status);
        if ((int16_t)*status_ret == 0) {
            *status_ret = local_status;
        }
        ACL_$EXIT_SUPER();
        return;
    }

    /* Extract UID of existing file */
    old_file_uid.high = *((uint32_t *)(entry + 0x28));
    old_file_uid.low = *((uint32_t *)(entry + 0x2c));

    /* Check rights on existing file */
    ACL_$RIGHTS(&old_file_uid,
                (boolean *)&dir_$old_add_baku_ignore_super_00e5716c,
                (uint32_t *)&dir_$old_add_baku_rights_00e56946,
                (int16_t *)&dir_$old_add_baku_acl_opts_00e5472e, status_ret);
    if (*status_ret != status_$ok) {
        if (*status_ret == status_$file_object_not_found) {
            *status_ret = status_$naming_branch_is_not_a_directory;
        } else {
            NAME_CONVERT_ACL_STATUS(status_ret);
        }
        NAME_$UNLOCK_DIR(&local_status);
        if ((int16_t)*status_ret == 0) {
            *status_ret = local_status;
        }
        ACL_$EXIT_SUPER();
        return;
    }

    /* Build .BAK name for mapped (uppercase) name */
    if ((int16_t)parsed_len < 0x1d) {
        name_offset = parsed_len + 4;
    } else {
        name_offset = 0x20;
    }
    /* Append ".BAK" (uppercase) to parsed name */
    parsed_name[name_offset - 4] = 0x2E; /* '.' */
    parsed_name[name_offset - 3] = 0x42; /* 'B' */
    parsed_name[name_offset - 2] = 0x41; /* 'A' */
    parsed_name[name_offset - 1] = 0x4B; /* 'K' */

    /* Check if .BAK entry already exists */
    bak_found = dir_$old_find_entry(handle, parsed_name, name_offset,
                             &bak_entry, &param5, &param6);
    if (bak_found < 0) {
        /* .BAK exists - check type */
        if (*((uint8_t *)(bak_entry + 0x27)) != 0x01) {
            *status_ret = status_$naming_invalid_link_operation;
            NAME_$UNLOCK_DIR(&local_status);
            if ((int16_t)*status_ret == 0) {
                *status_ret = local_status;
            }
            ACL_$EXIT_SUPER();
            return;
        }
        /* Check rights on .BAK file */
        ACL_$RIGHTS((uid_t *)(bak_entry + 0x28),
                    (boolean *)&dir_$old_add_baku_ignore_super_00e5716c,
                    (uint32_t *)&dir_$old_add_baku_rights_00e56946,
                    (int16_t *)&dir_$old_add_baku_acl_opts_00e5472e,
                    status_ret);
        if (*status_ret != status_$ok) {
            if (*status_ret == status_$file_object_not_found) {
                *status_ret = status_$naming_branch_is_not_a_directory;
            } else {
                NAME_CONVERT_ACL_STATUS(status_ret);
            }
            NAME_$UNLOCK_DIR(&local_status);
            if ((int16_t)*status_ret == 0) {
                *status_ret = local_status;
            }
            ACL_$EXIT_SUPER();
            return;
        }
    }

    /* Release directory lock */
    NAME_$UNLOCK_DIR(status_ret);
    if (*status_ret != status_$ok) {
        ACL_$EXIT_SUPER();
        return;
    }

    /* Get attributes from old file */
    FILE_$GET_ATTRIBUTES(&old_file_uid, &ACL_TYPE_DIR, &DAT_00e56094,
                         &attr_buf, attr_buf2, status_ret);
    if ((int16_t)*status_ret != 0) {
        ACL_$EXIT_SUPER();
        return;
    }

    /* Set protection on new file using old file's attributes */
    FILE_$SET_PROT(backup_uid, &DAT_00e5716a, attr_data, &prot_uid, status_ret);
    if ((int16_t)*status_ret != 0) {
        ACL_$EXIT_SUPER();
        return;
    }

    /* If .BAK existed, drop it first */
    if (bak_found < 0) {
        /* Drop old .BAK entry */
        /* 0x00E570E8-0x00E570EC: `st`, `st`, `clr.w` - check_del_right and
         * no_lock TRUE, allow_link FALSE. */
        NAME_$OLD_DELETE_ENTRYU(dir_uid, name_buf + 4, bak_name_len,
                     true, true, false, result_buf, status_ret);
        if (*status_ret != status_$ok) {
            ACL_$EXIT_SUPER();
            return;
        }
    }

    /* Rename old entry to .bak */
    {
        int16_t bak_len_s = bak_name_len;
        DIR_$OLD_CNAMEU(dir_uid, name, name_len,
                        name_buf + 4, &bak_len_s, status_ret);
    }
    if (*status_ret != status_$ok) {
        ACL_$EXIT_SUPER();
        return;
    }

    /* Add new file with original name */
    DIR_$OLD_ADD_HARD_LINKU(dir_uid, name, name_len, backup_uid, status_ret);
    if (*status_ret != status_$ok) {
        ACL_$EXIT_SUPER();
        return;
    }

    /* Flush */
    FILE_$FW_FILE(dir_uid, status_ret);

    ACL_$EXIT_SUPER();
}

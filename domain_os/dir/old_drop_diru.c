/*
 * DIR_$OLD_DROP_DIRU - Legacy drop/delete a directory
 *
 * Removes a directory entry from its parent after verifying permissions,
 * checking the directory is empty, and cleaning up ACLs.
 *
 * Original address: 0x00E5734C
 * Original size: 530 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_DROP_DIRU - Legacy drop/delete a directory
 *
 * Based on the Ghidra decompilation at 0x00E5734C:
 * 1. Look up the entry via DIR_$OLD_GET_ENTRYU
 * 2. Check entry type (reject type 3 = link)
 * 3. Check ACL rights on parent directory
 * 4. Check ACL rights on the directory to be dropped
 * 5. Enter super mode / acquire directory lock
 * 6. Check directory is empty (entry count at offset 0x16)
 * 7. Set default ACLs to NIL
 * 8. Get location info for the directory
 * 9. If remote: drop via REM_FILE, else: delete object locally
 * 10. Fix root entry and clean up
 *
 * Parameters:
 *   parent_uid - UID of parent directory
 *   name       - Name of directory to drop (also used as param_2/name ptr)
 *   name_high  - High part of name length (legacy calling convention)
 *   name_low   - Low part of name length (legacy calling convention)
 *   status_ret - Output: status code
 */
/*
 * Constant cells for the two ACL_$RIGHTS calls (0x00E573A6 and 0x00E573D2),
 * addressed with `pea (d,PC)` (PC = instruction address + 2).  The two calls
 * share the boolean and the option word but use DIFFERENT rights masks.
 */

/* 0x00E5716C, byte 0xFF: ACL_$RIGHTS' ignore_super argument (TRUE - the
 * super-user bypass is suppressed).  `pea (-0x236,PC)` at 0x00E573A0 and
 * `pea (-0x260,PC)` at 0x00E573CA. */
static const boolean dir_$old_drop_diru_ignore_super_00e5716c = true;

/* 0x00E56946, longword 0x00000002: the rights mask required on the PARENT
 * directory.  `pea (-0xa58,PC)` at 0x00E5739C. */
static const uint32_t dir_$old_drop_diru_parent_rights_00e56946 = 0x00000002;

/* 0x00E5755E, longword 0x00000040: the rights mask required on the directory
 * being dropped.  `pea (0x196,PC)` at 0x00E573C6.  This site previously used
 * the parent's 0x00E56946 cell. */
static const uint32_t dir_$old_drop_diru_dir_rights_00e5755e = 0x00000040;

/* 0x00E54B26, word 0x0001: ACL_$RIGHTS' option flags (object type 1,
 * directory).  `pea (-0x2874,PC)` at 0x00E57398 and `pea (-0x289e,PC)` at
 * 0x00E573C2. */
static const int16_t dir_$old_drop_diru_acl_opts_00e54b26 = 1;

void DIR_$OLD_DROP_DIRU(uid_t *parent_uid, char *name, uint16_t *name_high,
                        uint16_t *name_low, status_$t *status_ret)
{
    /* Local variables matching Ghidra decompilation */
    int16_t entry_type;           /* local_6c - entry type from GET_ENTRYU */
    uint32_t entry_uid_high;      /* uStack_6a */
    uint32_t entry_uid_low;       /* uStack_66 */
    uid_t dir_uid;                /* local_54 - UID of directory to drop */
    /* A6-0x48: the 0x20-byte object-location record.  The directory UID
     * goes in at +0x08 (0x00E5749C) and bit 6 of the flags byte at +0x1D is
     * cleared (0x00E574A4 `bclr.b #6,(-0x2b,A6)`). */
    file_$obj_loc_t location_buf;
    uint8_t entry_data[48];       /* entry buffer from GET_ENTRYU */
    uint32_t handle;              /* local_78 */
    uint32_t loc_buf1;            /* 0x00e574b2 pea (-0x70,A6) - never touched */
    uint32_t loc_buf2;            /* 0x00e574ae pea (-0x6c,A6) - aote+0x08 out */
    uint8_t parsed_name[32];      /* auStack_24 */
    uint16_t parsed_len[2];       /* local_7c */
    status_$t local_status;
    int16_t rights_result;
    int8_t is_empty;
    int8_t valid;
    /* A6-0x7A: FILE_$DELETE_OBJ's word output (0x00E57516) */
    uint16_t delete_out;

    /* Step 1: Look up the entry */
    DIR_$OLD_GET_ENTRYU(parent_uid, name, name_high,
                        &entry_type, status_ret);
    if (*status_ret != status_$ok) {
        return;
    }

    /* Step 2: Check entry type - reject links (type 3) */
    if (entry_type == 3) {
        *status_ret = status_$naming_invalid_link_operation;
        return;
    }

    /* Step 3: Check ACL rights on parent directory */
    ACL_$RIGHTS(parent_uid,
                (boolean *)&dir_$old_drop_diru_ignore_super_00e5716c,
                (uint32_t *)&dir_$old_drop_diru_parent_rights_00e56946,
                (int16_t *)&dir_$old_drop_diru_acl_opts_00e54b26, status_ret);
    if (*status_ret != status_$ok) {
        NAME_CONVERT_ACL_STATUS(status_ret);
        return;
    }

    /* Extract UID from entry data */
    /* The entry structure has UID at offsets matching uStack_6a/uStack_66 */
    dir_uid.high = entry_uid_high;
    dir_uid.low = entry_uid_low;

    /* Step 4: Check ACL rights on the directory to be dropped */
    rights_result = ACL_$RIGHTS(&dir_uid,
                                (boolean *)&dir_$old_drop_diru_ignore_super_00e5716c,
                                (uint32_t *)&dir_$old_drop_diru_dir_rights_00e5755e,
                                (int16_t *)&dir_$old_drop_diru_acl_opts_00e54b26,
                                status_ret);
    if (rights_result == 0x40) {
        *status_ret = status_$insufficient_rights_to_perform_operation;
        return;
    }
    /* Allow through if rights check returned no_right or insufficient_rights */
    if (*status_ret == status_$no_right_to_perform_operation ||
        *status_ret == status_$insufficient_rights_to_perform_operation) {
        *status_ret = status_$ok;
    }
    if (*status_ret != status_$ok) {
        NAME_CONVERT_ACL_STATUS(status_ret);
        return;
    }

    /* Step 5: Enter super mode / acquire directory lock */
    NAME_$LOCK_DIR(&dir_uid, &handle, 4, 0, status_ret);
    if (*status_ret != status_$ok) {
        ACL_$EXIT_SUPER();
        return;
    }

    /* Step 6: Check directory is empty */
    /* Assembly: tst.w (0x16,A0) - test entry count at offset 0x16 of handle */
    is_empty = (*((int16_t *)(handle + 0x16)) == 0) ? (int8_t)-1 : 0;
    if (is_empty < 0) {
        /* Clear the entry count field */
        *((uint16_t *)(handle + 0x18)) = 0;
    }

    /* Release directory lock */
    NAME_$UNLOCK_DIR(status_ret);
    if ((int16_t)*status_ret != 0) {
        ACL_$EXIT_SUPER();
        return;
    }

    if (is_empty >= 0) {
        /* Directory is not empty */
        *status_ret = status_$naming_directory_not_empty;
        ACL_$EXIT_SUPER();
        return;
    }

    /* Step 7: Set default ACLs to NIL */
    DIR_$OLD_SET_DEFAULT_ACL(&dir_uid, &ACL_$DIR_ACL,
                             &ACL_$NIL, status_ret);
    if ((int16_t)*status_ret != 0) {
        ACL_$EXIT_SUPER();
        return;
    }

    DIR_$OLD_SET_DEFAULT_ACL(&dir_uid, &ACL_$FILE_ACL,
                             &ACL_$NIL, status_ret);
    if ((int16_t)*status_ret != 0) {
        ACL_$EXIT_SUPER();
        return;
    }

    /* Step 8: Get location info (0x00E574BE) */
    location_buf.uid.high = dir_uid.high;
    location_buf.uid.low = dir_uid.low;
    location_buf.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;
    AST_$GET_LOCATION(&location_buf, 1, &loc_buf1, &loc_buf2, status_ret);
    if (*status_ret != status_$ok) {
        ACL_$EXIT_SUPER();
        return;
    }

    /* Step 9: Check if remote or local and delete accordingly */
    /* 0x00E574CE `tst.b (-0x2b,A6)` / bpl: the record's flags byte at +0x1D */
    if (location_buf.flags < 0) {
        /* Remote directory - use REM_FILE to drop */
        valid = name_$validate_leaf(name, *name_high, parsed_name, parsed_len);
        if (valid < 0) {
            REM_FILE_$DROP_HARD_LINKU(&location_buf.loc_info, parent_uid,
                                      parsed_name, parsed_len[0], 0, status_ret);
        } else {
            *status_ret = status_$naming_invalid_leaf;
        }
    } else {
        /* Local directory - delete object */
        /* 0x00E57516 pea's a word cell at A6-0x7A, not the location record. */
        FILE_$DELETE_OBJ(&dir_uid, (int8_t)0xFF, &delete_out, status_ret);
        if (*status_ret == status_$ok) {
            /* Fix root entry */
            name_$old_drop_entry(parent_uid, name, *name_high, 0, &dir_uid, status_ret);
        }
    }

    ACL_$EXIT_SUPER();
}

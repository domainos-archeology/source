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
void DIR_$OLD_DROP_DIRU(uid_t *parent_uid, char *name, uint16_t *name_high,
                        uint16_t *name_low, status_$t *status_ret)
{
    /* Local variables matching Ghidra decompilation */
    int16_t entry_type;           /* local_6c - entry type from GET_ENTRYU */
    uint32_t entry_uid_high;      /* uStack_6a */
    uint32_t entry_uid_low;       /* uStack_66 */
    uid_t dir_uid;                /* local_54 - UID of directory to drop */
    uint8_t location_buf[32];     /* auStack_3c + auStack_24 */
    uint8_t entry_data[48];       /* entry buffer from GET_ENTRYU */
    uint32_t handle;              /* local_78 */
    uint8_t loc_buf1[4];          /* auStack_74 */
    uint8_t loc_buf2[4];          /* auStack_70 */
    uint8_t parsed_name[32];      /* auStack_24 */
    uint16_t parsed_len[2];       /* local_7c */
    status_$t local_status;
    int16_t rights_result;
    int8_t is_empty;
    int8_t valid;
    uint8_t attr_byte;            /* local_2f */

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
    ACL_$RIGHTS(parent_uid, &DAT_00e5716c, &DAT_00e56946,
                &ACL_TYPE_DIR, status_ret);
    if (*status_ret != status_$ok) {
        NAME_CONVERT_ACL_STATUS(status_ret);
        return;
    }

    /* Extract UID from entry data */
    /* The entry structure has UID at offsets matching uStack_6a/uStack_66 */
    dir_uid.high = entry_uid_high;
    dir_uid.low = entry_uid_low;

    /* Step 4: Check ACL rights on the directory to be dropped */
    rights_result = ACL_$RIGHTS(&dir_uid, &DAT_00e5716c, &DAT_00e56946,
                                &ACL_TYPE_DIR, status_ret);
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
    FUN_00e54854(&dir_uid, &handle, 0x40000, status_ret);
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
    FUN_00e54734(status_ret);
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

    /* Step 8: Get location info */
    /* TODO: The following is simplified from the Ghidra output.
     * The original code uses AST_$GET_LOCATION and checks attr_byte
     * to determine if the directory is remote or local. */
    AST_$GET_LOCATION(location_buf, 1, loc_buf1, loc_buf2, status_ret);
    if (*status_ret != status_$ok) {
        ACL_$EXIT_SUPER();
        return;
    }

    /* Step 9: Check if remote or local and delete accordingly */
    attr_byte = location_buf[0x0d]; /* local_2f maps to byte in location data */
    if ((int8_t)attr_byte < 0) {
        /* Remote directory - use REM_FILE to drop */
        valid = FUN_00e54414(name, *name_high, parsed_name, parsed_len);
        if (valid < 0) {
            REM_FILE_$DROP_HARD_LINKU(location_buf + 0x10, parent_uid,
                                      parsed_name, parsed_len[0], 0, status_ret);
        } else {
            *status_ret = status_$naming_invalid_leaf;
        }
    } else {
        /* Local directory - delete object */
        FILE_$DELETE_OBJ(&dir_uid, 0xFF, location_buf, status_ret);
        if (*status_ret == status_$ok) {
            /* Fix root entry */
            FUN_00e56a04(parent_uid, name, *name_high, 0);
        }
    }

    ACL_$EXIT_SUPER();
}

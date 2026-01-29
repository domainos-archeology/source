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
 * 1. Validate leaf name via FUN_00e54414
 * 2. Compute backup name (name + ".bak", max 32 chars)
 * 3. Enter super mode / acquire directory lock
 * 4. Look up existing entry via FUN_00e54b9e
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
    uint8_t acl_data[48];
    uint8_t attr_buf[32];
    uint8_t attr_buf2[72];
    uint32_t attr_data[16];
    uint8_t result_buf[8];
    status_$t local_status;
    int16_t name_offset;
    int16_t i;

    /* Step 1: Validate leaf name */
    valid = FUN_00e54414(name, *name_len, parsed_name, &parsed_len);
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
    FUN_00e54854(dir_uid, &handle, 0x40000, status_ret);
    if ((int16_t)*status_ret != 0) {
        ACL_$EXIT_SUPER();
        return;
    }

    /* Step 4: Look up existing entry */
    found = FUN_00e54b9e(handle, parsed_name, parsed_len,
                         &entry, &param5, &param6);

    if (found >= 0) {
        /* Step 5: Entry not found - simple add path */
        FUN_00e54734(status_ret);
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
        FUN_00e54734(&local_status);
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
    ACL_$RIGHTS(&old_file_uid, &DAT_00e5716c, &DAT_00e56946,
                &ACL_TYPE_FILE, status_ret);
    if (*status_ret != status_$ok) {
        if (*status_ret == status_$wrong_type) {
            *status_ret = status_$naming_name_is_not_a_file;
        } else {
            NAME_CONVERT_ACL_STATUS(status_ret);
        }
        FUN_00e54734(&local_status);
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
    bak_found = FUN_00e54b9e(handle, parsed_name, name_offset,
                             &bak_entry, &param5, &param6);
    if (bak_found < 0) {
        /* .BAK exists - check type */
        if (*((uint8_t *)(bak_entry + 0x27)) != 0x01) {
            *status_ret = status_$naming_invalid_link_operation;
            FUN_00e54734(&local_status);
            if ((int16_t)*status_ret == 0) {
                *status_ret = local_status;
            }
            ACL_$EXIT_SUPER();
            return;
        }
        /* Check rights on .BAK file */
        ACL_$RIGHTS((uid_t *)(bak_entry + 0x28), &DAT_00e5716c,
                    &DAT_00e56946, &ACL_TYPE_FILE, status_ret);
        if (*status_ret != status_$ok) {
            if (*status_ret == status_$wrong_type) {
                *status_ret = status_$naming_name_is_not_a_file;
            } else {
                NAME_CONVERT_ACL_STATUS(status_ret);
            }
            FUN_00e54734(&local_status);
            if ((int16_t)*status_ret == 0) {
                *status_ret = local_status;
            }
            ACL_$EXIT_SUPER();
            return;
        }
    }

    /* Release directory lock */
    FUN_00e54734(status_ret);
    if (*status_ret != status_$ok) {
        ACL_$EXIT_SUPER();
        return;
    }

    /* Get attributes from old file */
    FILE_$GET_ATTRIBUTES(&old_file_uid, &ACL_TYPE_DIR, &DAT_00e56094,
                         attr_buf, attr_buf2, status_ret);
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
        FUN_00e56b08(dir_uid, name_buf + 4, bak_name_len,
                     0xFF, 0xFF, 0, result_buf, status_ret);
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

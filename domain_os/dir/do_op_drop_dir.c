/*
 * dir_$do_op_drop_dir - DO_OP handler: drop/delete directory entry
 *
 * Removes a directory entry from its parent directory. Opens the parent
 * for write access with ACL right 3 (delete), looks up the entry by name,
 * and performs several validation checks:
 *
 * 1. Rejects link entries (type 4) - use drop_link instead
 * 2. Checks if the target UID is in the directory lock list (prevents
 *    deletion of directories that are currently open/locked)
 * 3. Verifies ACL rights on the target object
 * 4. If the target is a directory on the same volume, verifies it's
 *    empty (contains only 1 entry) before allowing deletion
 * 5. Truncates any ACL objects associated with the directory
 * 6. Deletes the directory object via FILE_$DELETE_OBJ
 * 7. Removes the name entry from the parent
 *
 * Falls back to DIR_$OLD_DROP_DIRU if status is naming_bad_directory.
 *
 * Called by DIR_$DO_OP case 0x3A. Audit code 0x17.
 *
 * Parameters:
 *   uid        - Parent directory UID
 *   name       - Entry name to drop
 *   name_len   - Length of name
 *   status_ret - Output: status code
 *
 * Original address: 0x00E52744
 * Original size: 682 bytes
 */

#include "dir/dir_internal.h"

/* DAT_00e4bc24 - ACL rights mask (0xFF = all rights) */
extern uint8_t DAT_00e4bc24;

/* DAT_00e4b444 - ACL check parameter */
extern uint8_t DAT_00e4b444;

/* DAT_00e51b64 - ACL rights value for directory delete (0x00000040) */
extern uint32_t DAT_00e51b64;

void dir_$do_op_drop_dir(uid_t *uid, void *name, uint16_t name_len,
                         status_$t *status_ret)
{
    char *a5 = (char *)__A5_BASE();
    uint32_t parent_handle;
    uint32_t child_handle;
    void *parent_h;
    uid_t target_uid;
    uid_t acl_uid;
    char found;
    uint8_t *entry_ptr;
    void *entry_ret;
    uint8_t extra1[2];
    uint8_t extra2[4];
    status_$t local_status;

    /*
     * AST location descriptor struct (30+ bytes)
     * Layout relative to loc_desc base:
     *   +0x00: location result (2 bytes)
     *   +0x02: vol_id (2 bytes, int16_t)
     *   +0x08: uid.high (4 bytes, input)
     *   +0x0C: uid.low (4 bytes, input)
     *   +0x1D: flags byte (bit 6 cleared before call)
     */
    uint8_t loc_desc[30];
    uint8_t get_loc_buf1[4];
    uint8_t get_loc_buf2[4];

    uint8_t delete_buf[8];
    uint8_t remove_buf[8];

    child_handle = 0;

    ACL_$ENTER_SUPER();

    /* Open parent directory with write access, ACL right 3 (delete) */
    dir_$open_dir(uid, 2, 3, &parent_handle, status_ret);
    parent_h = (void *)parent_handle;
    if (*status_ret != status_$ok) {
        goto done;
    }

    /* Look up the entry by name */
    found = FUN_00e4c9e4(parent_h, name, name_len, 0,
                         &entry_ret, extra1, extra2);
    entry_ptr = (uint8_t *)entry_ret;
    if (found >= 0) {
        /* Entry not found */
        *status_ret = status_$naming_name_not_found;
        goto done;
    }

    /* Reject link entries (type & 7 == 4) */
    if ((*entry_ptr & 7) == 4) {
        *status_ret = status_$naming_invalid_link_operation;
        goto done;
    }

    /* Copy target UID from entry (offset +4) */
    target_uid.high = *(uint32_t *)(entry_ptr + 4);
    target_uid.low = *(uint32_t *)(entry_ptr + 8);

    /* Check directory lock list - prevent deletion of open directories */
    {
        int16_t lock_count = *(int16_t *)(a5 + 0x155A);
        uint16_t i = lock_count - 1;
        if ((int16_t)i >= 0) {
            char *lock_base = a5 + 0x155C;
            do {
                if (target_uid.high == *(uint32_t *)(lock_base) &&
                    target_uid.low == *(uint32_t *)(lock_base + 4)) {
                    *status_ret = status_$naming_directory_locked;
                    goto done;
                }
                lock_base += 8;
                i--;
            } while (i != 0xFFFF);
        }
    }

    /* Set up location descriptor with target UID */
    *(uint32_t *)(loc_desc + 0x08) = target_uid.high;
    *(uint32_t *)(loc_desc + 0x0C) = target_uid.low;
    /* Clear bit 6 of flags byte in descriptor */
    loc_desc[0x1D] &= 0xBF;

    /* Get location info for the target */
    AST_$GET_LOCATION(loc_desc, 1, get_loc_buf1, get_loc_buf2, &local_status);

    /* Check if target is on same volume as parent and is local */
    if (*(int16_t *)(loc_desc + 0x02) == *(int16_t *)((char *)parent_h + 0x3A) &&
        local_status == status_$ok &&
        (int8_t)loc_desc[0x1D] >= 0) {

        /* Same volume, local object - check ACL rights */
        int16_t acl_result;
        acl_result = ACL_$RIGHTS(&target_uid, &DAT_00e4bc24,
                                 &DAT_00e51b64, &DAT_00e4b444, status_ret);

        if (acl_result == 0x40) {
            *status_ret = status_$naming_insufficient_rights;
            goto done;
        }

        {
            status_$t acl_status = *status_ret;
            if (acl_status != status_$ok &&
                acl_status != status_$no_right_to_perform_operation &&
                acl_status != status_$insufficient_rights_to_perform_operation) {
                goto done;
            }
        }

        /* Open the target directory to check if empty */
        dir_$open_dir(&target_uid, 2, 0, &child_handle, status_ret);
        if (*status_ret != status_$ok) {
            goto done;
        }

        /* Map page 0 of the target directory */
        {
            uint8_t *child_page;
            child_page = (uint8_t *)dir_$map_page((void *)child_handle, 0);

            /* Check entry type - must be 0 (normal/leaf page) */
            if ((*child_page >> 6) != 0) {
                *status_ret = status_$naming_directory_not_empty;
                goto done;
            }

            /* Calculate number of entries:
             * entry_count = (page[0x0E] - (page[0x14] + 0x12)) / 2
             * Directory is "empty" if it has exactly 1 entry (the "." entry) */
            {
                int32_t free_offset = (int32_t)*(int16_t *)(child_page + 0x0E);
                int32_t base_offset = (int32_t)(int16_t)(*(int16_t *)(child_page + 0x14) + 0x12);
                int32_t diff = free_offset - base_offset;
                if (diff < 0) {
                    diff += 1;
                }

                if ((diff >> 1) != 1) {
                    *status_ret = status_$naming_directory_not_empty;
                    goto done;
                }
            }

            /* Directory is empty - proceed with deletion */

            /* Truncate DIR ACL if present */
            if (child_page[0x46] != 0) {
                acl_uid.high = *(uint32_t *)(child_page + 0x46);
                acl_uid.low = *(uint32_t *)(child_page + 0x4A);
                AST_$TRUNCATE(&acl_uid, 0, 3, delete_buf, &local_status);
            }

            /* Truncate FILE ACL if present */
            if (child_page[0x7A] != 0) {
                acl_uid.high = *(uint32_t *)(child_page + 0x7A);
                acl_uid.low = *(uint32_t *)(child_page + 0x7E);
                AST_$TRUNCATE(&acl_uid, 0, 3, delete_buf, &local_status);
            }

            /* Unmap the child directory pages */
            FUN_00e4b6ba((void *)child_handle);

            /* Delete the directory object */
            FILE_$DELETE_OBJ(&target_uid, 0xFF, delete_buf, status_ret);
            if (*status_ret != status_$ok) {
                goto done;
            }
        }

        goto remove_entry;
    } else {
        /* Different volume or object not found - just remove the entry */
        *status_ret = local_status;
        if (*status_ret != status_$ok &&
            *status_ret != file_$object_not_found) {
            goto done;
        }

remove_entry:
        /* Remove the name entry from the parent directory */
        FUN_00e50fc8(parent_h, name, name_len, 2, remove_buf, status_ret);
    }

done:
    dir_$release_handle(&parent_handle);
    dir_$release_handle(&child_handle);

    ACL_$EXIT_SUPER();

    /* Fall back to legacy implementation if bad directory */
    if (*status_ret == status_$naming_bad_directory) {
        /* DIR_$OLD_DROP_DIRU takes a pointer to name_len; on m68k the
         * 4-byte pea pushes the address of the 2-byte name_len stack
         * parameter, which the callee reads as two 16-bit words. */
        DIR_$OLD_DROP_DIRU(uid, name, &name_len, &name_len, status_ret);
    }
}

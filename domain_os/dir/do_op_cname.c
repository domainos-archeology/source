/*
 * dir_$do_op_cname - DO_OP handler for rename (change name)
 *
 * Server-side handler for opcode 0x32 (DIR_OP_CNAMEU) in DIR_$DO_OP.
 * Renames a directory entry from old_name to new_name. The implementation
 * adds the entry under the new name first, then removes the old entry.
 * This ensures the entry is never "lost" during the rename.
 *
 * Process:
 * 1. Enter super mode, open directory for write with ACL right 2
 * 2. Look up old_name:
 *    a. Not found: for server processes (type 9), check if new_name
 *       already exists (idempotent rename), return name_not_found
 *    b. Found: extract entry type and data
 * 3. Based on entry type:
 *    - Type 2 (file): check ACL rights on the file UID; if server process,
 *      check ACL attributes for read-only flag; check directory lock list
 *    - Type 3 (hard link): extract UID + extra data
 *    - Type 4 (soft link): extract link length and data; for inline links
 *      (page index == -1), data follows entry; for overflow links, map
 *      the overflow page and copy link data via link buffer mutex
 *    - Other: CRASH_SYSTEM
 * 4. Add entry under new_name with the extracted data
 * 5. Release link buffer mutex if held
 * 6. Remove old entry (only if add succeeded)
 *
 * Parameters:
 *   uid            - Directory UID
 *   old_name_info  - Old name pointer (packed with type info)
 *   old_name_len   - Old name length
 *   new_name_info  - New name pointer (packed with type info)
 *   new_name_len   - New name length
 *   status_info    - Status return pointer (packed)
 *
 * Note: The Ghidra decompilation for this function is heavily corrupted
 * by CONCAT and packed parameter issues. The assembly shows the actual
 * parameter passing more clearly. Parameters are passed on the stack as:
 *   +0x08: uid (4 bytes pointer)
 *   +0x0E: old_name (4 bytes pointer, in D4)
 *   +0x12: old_name_len (2 bytes)
 *   +0x14: new_name (4 bytes pointer, in D5)
 *   +0x18: new_name_len (2 bytes)
 *   +0x1A: status_ret (4 bytes pointer, in A4)
 *
 * Original address: 0x00E518BC
 * Original size: 330 bytes (main body) + jump table
 */

#include "dir/dir_internal.h"

/* DAT_00e505c4 - ACL option flags for cname rights check */

/* DAT_00e51b64 - ACL rights mask (0x00000040 = rename right) */

void dir_$do_op_cname(uid_t *uid, uint16_t req_version,
                      void *old_name, uint16_t old_name_len,
                      void *new_name, uint16_t new_name_len,
                      status_$t *status_ret)
{
    /* A6+0x0C is never read by this routine; it is part of the frame the
     * caller builds (0x00E4C466 `move.w (0xe,A2),-(SP)`). */
    (void)req_version;

    char *a5 = (char *)__A5_BASE();
    uint32_t local_handle;
    void *entry_ptr;
    uint16_t extra_buf[2];
    uint8_t lookup_buf[32];
    uint16_t entry_type;
    uint32_t entry_extra;      /* For type 3 entries */
    uid_t entry_uid;
    int16_t link_len = 0;     /* For type 4 entries */
    char *link_data = NULL;    /* For type 4 entries */
    uint8_t acl_attrs[8];
    uint8_t acl_buf[40];
    char acl_flag;

    ACL_$ENTER_SUPER();

    /* Open directory for write with ACL right 2 (modify) */
    dir_$open_dir(uid, 2, 2, &local_handle, status_ret);
    if (*status_ret != status_$ok) {
        goto done;
    }

    /* Look up the old name */
    {
        char found;
        found = dir_$find_entry((void *)(uintptr_t)local_handle,
                                old_name, old_name_len, 8,
                                &entry_ptr, lookup_buf, extra_buf);

        if (found >= 0) {
            /* Old name not found */
            /* Idempotent handling: for server processes, check if new name exists */
            if (*(int16_t *)((char *)PROC1_$TYPE +
                (int16_t)(PROC1_$CURRENT * 2)) == 9) {
                char found2;
                found2 = dir_$find_entry((void *)(uintptr_t)local_handle,
                                         new_name, new_name_len, 8,
                                         &entry_ptr, lookup_buf, extra_buf);
                if (found2 < 0) {
                    /* New name exists - idempotent rename already done */
                    goto done;
                }
            }
            *status_ret = status_$naming_name_not_found;
            goto done;
        }
    }

    /* Extract entry type */
    {
        uint8_t *ep = (uint8_t *)entry_ptr;
        entry_type = *ep & 7;

        switch (entry_type) {
        case 2:
            /* File entry - extract UID */
            entry_uid.high = *(uint32_t *)(ep + 4);
            entry_uid.low = *(uint32_t *)(ep + 8);

            /* For server processes (type 9), check ACL read-only flag */
            if (*(int16_t *)((char *)PROC1_$TYPE +
                (int16_t)(PROC1_$CURRENT * 2)) == 9) {
                /* Clear bit 6 of flags */
                /* Check ACL attributes */
                ACL_$GET_ACL_ATTRIBUTES(&entry_uid, 0x80, acl_buf, status_ret);
                if (*status_ret == status_$ok && (int8_t)acl_flag < 0) {
                    /* Read-only entry */
                    *status_ret = status_$naming_invalid_link_operation;
                    goto done;
                }
            }

            /* Check directory lock list - prevent rename of locked entries */
            {
                int16_t lock_count = *(int16_t *)(a5 + 0x155A);
                int16_t i = lock_count - 1;
                char *lock_base = a5;
                if (i >= 0) {
                    do {
                        if (entry_uid.high == *(uint32_t *)(lock_base + 0x155C) &&
                            entry_uid.low == *(uint32_t *)(lock_base + 0x1560)) {
                            *status_ret = status_$naming_directory_locked;
                            goto done;
                        }
                        i--;
                        lock_base += 8;
                    } while (i != -1);
                }
            }

            /* Check ACL rights for rename */
            {
                uint32_t rights_result;
                rights_result = ACL_$RIGHTS(&entry_uid, &DAT_00e4bc24,
                                            &DAT_00e51b64, &DAT_00e505c4, status_ret);
                {
                    status_$t acl_status = *status_ret;
                    if (acl_status == status_$no_right_to_perform_operation ||
                        acl_status == status_$insufficient_rights_to_perform_operation ||
                        acl_status == status_$ok) {
                        if ((rights_result & 0x40) != 0) {
                            *status_ret = status_$naming_insufficient_rights;
                            goto done;
                        }
                    } else {
                        /* Clear high bit and check for wrong_type */
                        *status_ret &= 0x7FFFFFFF;
                        if (*status_ret != status_$wrong_type) {
                            NAME_CONVERT_ACL_STATUS(status_ret);
                            goto done;
                        }
                    }
                }
            }
            break;

        case 3:
            /* Hard link entry - extract UID + extra */
            entry_extra = *(uint32_t *)(ep + 0x0C);
            entry_uid.high = *(uint32_t *)(ep + 4);
            entry_uid.low = *(uint32_t *)(ep + 8);
            break;

        case 4:
            /* Soft link entry - extract link length and data */
            link_len = *(int16_t *)(ep + 2);

            /* Acquire link buffer mutex */
            ML_$EXCLUSION_START(&DIR_$LINK_BUF_MUTEX);
            *(int16_t *)(a5 + 0x2040) = PROC1_$CURRENT;

            if (*(int16_t *)(ep + 4) == -1) {
                /* Inline link data: starts at entry + name_len + 0x0C */
                link_data = (char *)(ep + ep[1] + 0x0C);
            } else {
                /* Overflow page link data */
                uint16_t ovf_page = *(uint16_t *)(ep + 4);
                uint8_t *ovf = (uint8_t *)dir_$map_page(
                    (void *)(uintptr_t)local_handle, ovf_page);
                link_data = (char *)(ovf + 1);
            }

            /* Copy link data to the link buffer (A5-relative) */
            {
                int16_t remaining = link_len - 1;
                char *dest = a5;
                if (remaining >= 0) {
                    do {
                        *dest = *link_data;
                        remaining--;
                        link_data++;
                        dest++;
                    } while (remaining != -1);
                }
            }
            /* Now link_data points to A5 (the link buffer copy) */
            link_data = a5;
            break;

        default:
            CRASH_SYSTEM(&Naming_bad_request_header_ver_err);
            break;
        }
    }

    /* Add entry under the new name */
    dir_$add_entry(local_handle, new_name, new_name_len, entry_type,
                   entry_extra, &entry_uid, link_len, link_data, status_ret);

    /* Release link buffer mutex if we acquired it */
    if (PROC1_$CURRENT == *(int16_t *)(a5 + 0x2040)) {
        *(int16_t *)(a5 + 0x2040) = 0;
        ML_$EXCLUSION_STOP(&DIR_$LINK_BUF_MUTEX);
    }

    /* If add succeeded, remove the old entry */
    if (*status_ret == status_$ok) {
        dir_$remove_entry((void *)(uintptr_t)local_handle,
                          old_name, old_name_len, entry_type,
                          &entry_uid, status_ret);
    }

done:
    dir_$release_handle(&local_handle);
    ACL_$EXIT_SUPER();
}

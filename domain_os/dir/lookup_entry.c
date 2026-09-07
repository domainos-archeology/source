/*
 * dir_$lookup_entry - Uncached directory entry lookup
 *
 * Originally a nested Pascal subprocedure of dir_$do_op_get_entryu (via
 * dir_$get_entry_cached). Performs the actual directory entry lookup by
 * opening the directory, calling dir_$find_entry, and interpreting the
 * result based on entry type.
 *
 * Entry types handled:
 *   Type 2: File/directory entry - returns UID, checks against the
 *           remap table (A5+0x155A..0x15A0) which maps UIDs to their
 *           network-redirected equivalents.
 *   Type 3: Hard link entry - returns UID and extra data.
 *   Type 4: Soft link entry - returns type_ret=3 (link indicator).
 *
 * For the root directory (NAME_$ROOT_UID), if the entry is not found
 * locally, queries remote nodes via REM_NAME_$GET_ENTRY. If found
 * remotely, unmaps the case and adds the entry locally via
 * dir_$do_op_add_entry.
 *
 * Parameters:
 *   uid        - Directory UID
 *   name       - Entry name
 *   name_len   - Length of name
 *   type_ret   - Output: entry type (1=file, 3=link)
 *   uid_ret    - Output: UID of the found entry
 *   extra_ret  - Output: extra data (type 3 entries only)
 *   found_ret  - Output: 0xFF if found in cache-eligible form, 0 otherwise
 *   status_ret - Output: status code
 *
 * Original address: 0x00E4CB6A
 * Original size: 538 bytes
 */

#include "dir/dir_internal.h"

/* Character classification bitmap for case unmapping at 0x00e4cd84
 * TODO(source-qgq): Replace with proper reference */

void dir_$lookup_entry(uid_t *uid, void *name, uint16_t name_len,
                       uint16_t *type_ret, uid_t *uid_ret,
                       uint32_t *extra_ret, uint8_t *found_ret,
                       status_$t *status_ret)
{
    char *a5 = (char *)__A5_BASE();
    uint32_t local_handle;
    uint8_t *entry_ptr;
    /* A6-0x4E: dir_$find_entry's depth_ret, cleared with `clr.w (A0)`
     * at 0x00E4C9F0 - a word, not a status longword. */
    int16_t find_depth;
    uint8_t find_extra2[2];       /* A6-0x4C: dir_$find_entry's extra array */
    uint8_t find_extra3[2];       /* A6-0x4A: dir_$do_op_add_entry's result */
    status_$t add_status;         /* A6-0x40: dir_$do_op_add_entry's status */
    int16_t local_type;
    int16_t remote_name_len;
    uint8_t remote_name[32];
    uint32_t remote_uid_high;
    uint32_t remote_uid_low;
    uint32_t remote_extra;

    *found_ret = 0;

    ACL_$ENTER_SUPER();

    /* Open directory in read mode with ACL right 1 */
    dir_$open_dir(uid, 1, 1, &local_handle, status_ret);
    if (*status_ret != status_$ok) {
        goto exit_super;
    }

    /* Look up the entry */
    char found = dir_$find_entry((void *)(uintptr_t)local_handle,
                                 name, name_len, 0,
                                 (void **)&entry_ptr, find_extra2,
                                 &find_depth);

    if (found < 0) {
        /* Entry found */
        *extra_ret = 0;

        uint8_t entry_type = *entry_ptr & 7;

        switch (entry_type) {
        case 2:
            /* File/directory entry */
            *type_ret = 1;
            uid_ret->high = *(uint32_t *)(entry_ptr + 4);
            uid_ret->low  = *(uint32_t *)(entry_ptr + 8);
            *found_ret = 0xFF;

            /* Check UID remap table */
            {
                int16_t remap_count = *(int16_t *)(a5 + 0x155A) - 1;
                if (remap_count >= 0) {
                    int16_t ri = 1;
                    char *rp = a5;
                    do {
                        if (uid_ret->high == *(uint32_t *)(rp + 0x155C) &&
                            uid_ret->low == *(uint32_t *)(rp + 0x1560)) {
                            *found_ret = 0;
                            rp = a5 + ri * 8;
                            uid_ret->high = *(uint32_t *)(rp + 0x1594);
                            uid_ret->low  = *(uint32_t *)(rp + 0x1598);
                            break;
                        }
                        ri++;
                        remap_count--;
                        rp += 8;
                    } while (remap_count != -1);
                }
            }
            break;

        case 3:
            /* Hard link entry - includes extra data */
            *type_ret = 1;
            uid_ret->high = *(uint32_t *)(entry_ptr + 4);
            uid_ret->low  = *(uint32_t *)(entry_ptr + 8);
            *extra_ret = *(uint32_t *)(entry_ptr + 0x0C);
            break;

        case 4:
            /* Soft link entry */
            *type_ret = 3;
            break;

        default:
            CRASH_SYSTEM(&Naming_bad_request_header_ver_err);
            break;
        }
    } else {
        /* Entry not found locally - check if this is the root directory */
        if (uid->high == NAME_$ROOT_UID.high &&
            uid->low == NAME_$ROOT_UID.low) {

            /* Release handle before remote query */
            dir_$release_handle(&local_handle);

            /* Query remote nodes */
            REM_NAME_$GET_ENTRY(uid, name, &name_len,
                                &local_type, status_ret);

            if (*status_ret == status_$ok && local_type == 1) {
                /* Remote entry found - unmap case on the name */
                int16_t remaining = remote_name_len - 1;
                if (remaining >= 0) {
                    int16_t j = 1;
                    do {
                        uint16_t ch = (uint16_t)(uint8_t)remote_name[j - 1];
                        int16_t diff = 0x5F - ch;

                        if (ch < 0x60) {
                            int16_t byte_idx = diff >> 3;
                            if ((*((uint8_t *)&PTR_DAT_00e4cd84 + (int16_t)byte_idx) &
                                 (1 << (remote_name[j - 1] & 7))) != 0) {
                                remote_name[j - 1] = remote_name[j - 1] + 0x20;
                            }
                        }
                        j++;
                        remaining--;
                    } while (remaining != -1);
                }

                /* Add the remote entry to the local directory */
                /* 0x00E4CD1C `pea (-0x33a,PC)` resolves to 0x00E4C9E4 -
                 * dir_$find_entry's entry point.  target_len is 0 here
                 * (`clr.w` at 0x00E4CD20) so the byte string is never read;
                 * the compiler simply emitted a code address.  target_data
                 * is a 32-bit VA cell, hence the cast.
                 * 0x00E4CD14 pushes a *separate* status cell at A6-0x40,
                 * not find_entry's depth word. */
                dir_$do_op_add_entry(uid, 0, remote_name, remote_name_len,
                                     3, remote_extra, &remote_uid_high, 0,
                                     (uint32_t)(uintptr_t)dir_$find_entry,
                                     find_extra3, &add_status);

                *type_ret = local_type;
                uid_ret->high = remote_uid_high;
                uid_ret->low  = remote_uid_low;
                *extra_ret = remote_extra;
                goto release_and_exit;
            }
        }

        /* Not found */
        *status_ret = status_$naming_name_not_found;
    }

release_and_exit:
    dir_$release_handle(&local_handle);

exit_super:
    ACL_$EXIT_SUPER();
}

/*
 * dir_$do_op_validate_root_entry - Validate and refresh a root directory entry
 *
 * Server-side handler for opcode 0x50 in DIR_$DO_OP. Validates that a named
 * entry in the root directory is still current by comparing it against the
 * authoritative copy from the replicated root (via REM_NAME_$GET_ENTRY).
 *
 * Process:
 * 1. Enter super mode
 * 2. Open root directory (mode 1, read) and look up the entry by name
 * 3. Extract the entry's UID and generation info
 * 4. Release the handle
 * 5. Query REM_NAME_$GET_ENTRY against the canned replicated root UID
 * 6. Compare the local and remote UIDs:
 *    a. If UIDs match and extra data matches: entry is current, done
 *    b. If first byte of local UID is non-zero, and first byte of remote UID
 *       is non-zero, and low 20 bits of UID.low match, and remote UID.high
 *       is newer (greater): entry is current (cached entry is older version), done
 * 7. If entry is stale: re-open root for write (mode 2), remove old entry,
 *    add new entry with remote data
 * 8. If updated: set status_$naming_cache_entry_stale_and_updated and
 *    update hints via HINT_$ADDI
 *
 * Parameters:
 *   name       - Entry name to validate
 *   name_len   - Length of name
 *   status_ret - Output: status code
 *
 * Original address: 0x00E501D2
 * Original size: 456 bytes
 */

#include "dir/dir_internal.h"

#ifndef status_$naming_cache_entry_stale
#define status_$naming_cache_entry_stale               0x000E0022
#endif
#ifndef status_$naming_cache_entry_stale_and_updated
#define status_$naming_cache_entry_stale_and_updated    0x000E0023
#endif

void dir_$do_op_validate_root_entry(void *name, uint16_t name_len,
                                    status_$t *status_ret)
{
    uint16_t slot_info[2];
    uint16_t extra_buf[2];
    uint32_t local_handle;
    void *entry_ptr;
    uint32_t local_uid_high;    /* -0x50 */
    uint32_t local_uid_low;     /* -0x4C */
    uint32_t local_extra;       /* D2 */

    /*
     * Remote entry buffer layout (from REM_NAME_$GET_ENTRY):
     * The entry buffer is at -0x48 (36 bytes).
     * Within it, at offset 0x24 from the buffer start (-0x24 from A6):
     *   remote_uid.high at -0x24
     *   remote_uid.low  at -0x20
     *   remote_extra    at -0x1C
     */
    uint8_t remote_entry_buf[36];
    uid_t remote_uid;           /* -0x24: uid from remote entry */
    uint32_t remote_extra;      /* -0x1C: extra/generation from remote entry */
    uint8_t remove_buf[8];      /* -0x18: buffer for remove_entry result */
    uint32_t hint_data[2];      /* -0x10, -0x0C: hint update data */

    ACL_$ENTER_SUPER();

    /* Open root directory for read (mode 1, rights 0) */
    dir_$open_dir(&NAME_$ROOT_UID, 1, 0, &local_handle, status_ret);
    if (*status_ret != status_$ok) {
        goto cleanup;
    }

    /* Look up the entry by name */
    {
        char found;
        found = dir_$find_entry((void *)(uintptr_t)local_handle, name, name_len, 0,
                                &entry_ptr, extra_buf, slot_info);
        if (found < 0) {
            uint8_t *ep = (uint8_t *)entry_ptr;

            /* Extract UID based on entry type */
            if ((*ep & 7) == 3) {
                /* Type 3 (hard link): UID at +4, +8, extra at +0x0C */
                local_uid_high = *(uint32_t *)(ep + 4);
                local_uid_low = *(uint32_t *)(ep + 8);
                local_extra = *(uint32_t *)(ep + 0x0C);
            } else {
                /* Type 2 (file): UID at +4, +8, no extra */
                local_uid_high = *(uint32_t *)(ep + 4);
                local_uid_low = *(uint32_t *)(ep + 8);
                local_extra = 0;
            }

            /* Release the read handle before querying remote */
            dir_$release_handle(&local_handle);

            /* Query the authoritative replicated root */
            REM_NAME_$GET_ENTRY(&NAME_$CANNED_REP_ROOT_UID, name,
                                &name_len, remote_entry_buf, status_ret);
            if (*status_ret != status_$ok) {
                goto cleanup;
            }

            /* Compare local vs remote UIDs */
            if (local_uid_high == remote_uid.high &&
                local_uid_low == remote_uid.low) {
                /* UIDs match - check extra data */
                if (local_extra == remote_extra) {
                    /* Entry is fully current */
                    goto cleanup;
                }
                /* Extra differs - fall through to update */
            } else {
                /* UIDs differ - check if local is a valid newer version */
                if ((uint8_t)(local_uid_high >> 24) != 0 &&
                    (uint8_t)(remote_uid.high >> 24) != 0 &&
                    (local_uid_low & 0xFFFFF) == (remote_uid.low & 0xFFFFF) &&
                    local_uid_high > remote_uid.high) {
                    /* Local version is newer (bhi = unsigned higher), keep it */
                    goto cleanup;
                }
                /* Local is stale - fall through to update */
            }

            /* Entry is stale - re-open root for write to update it */
            dir_$open_dir(&NAME_$ROOT_UID, 2, 0, &local_handle, status_ret);
            if (*status_ret != status_$ok) {
                goto cleanup;
            }

            /* Remove the old entry (op_type=0 means any type) */
            dir_$remove_entry((void *)(uintptr_t)local_handle, name,
                              name_len, 0, remove_buf, status_ret);
            if (*status_ret != status_$ok) {
                *status_ret = status_$naming_cache_entry_stale;
                goto cleanup;
            }

            /* Add the new entry with remote data (type 3, with extra) */
            dir_$add_entry(local_handle, name, name_len, 3,
                           remote_extra, &remote_uid, 0,
                           dir_$find_entry, status_ret);
            if (*status_ret == status_$ok) {
                *status_ret = status_$naming_cache_entry_stale_and_updated;
                goto cleanup;
            }
        }
    }

    /* Entry not found */
    *status_ret = status_$naming_name_not_found;

cleanup:
    dir_$release_handle(&local_handle);

    /* If we updated the entry, update hints */
    if (*status_ret == status_$naming_cache_entry_stale_and_updated) {
        hint_data[0] = remote_extra;
        hint_data[1] = remote_uid.low & 0xFFFFF;
        HINT_$ADDI(&remote_uid, hint_data);
    }

    ACL_$EXIT_SUPER();
}

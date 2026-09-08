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

void dir_$do_op_validate_root_entry(void *name, uint16_t name_len,
                                    status_$t *status_ret)
{
    /* A6-0x60: dir_$find_entry's depth_ret (`pea (-0x60,A6)`, 0x00E50208). */
    int16_t  find_depth;
    /* A6-0x5E: dir_$find_entry's path buffer.  Its capacity argument is 0
     * (`clr.w -(SP)` at 0x00E50214), so no level is ever recorded in it. */
    dir_$page_path_t find_path[1];
    uint32_t local_handle;      /* A6-0x5C */
    void    *entry_ptr;         /* A6-0x58 */
    uint32_t local_uid_high;    /* A6-0x50 */
    uint32_t local_uid_low;     /* A6-0x4C */
    uint32_t local_extra;       /* D2 */

    /*
     * A6-0x48: ONE dir_$rep_entry_t, the record REM_NAME_$GET_ENTRY fills
     * (`pea (-0x48,A6)` at 0x00E50268).  The consumers reach two of its
     * fields (source-knhm):
     *   (-0x24,A6) uid    = rep + 0x24  (0x00E5028C, 0x00E50324, 0x00E5037E)
     *   (-0x1C,A6) extra  = rep + 0x2C  (0x00E5029A, 0x00E50328, 0x00E50366)
     */
    dir_$rep_entry_t rep;
    uint8_t  remove_buf[8];     /* A6-0x18: dir_$remove_entry's uid_ret */
    uint32_t hint_data[2];      /* A6-0x10 / A6-0x0C: HINT_$ADDI's argument */

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
                                &entry_ptr, find_path, &find_depth);
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
            /* 0x00E50266-0x00E50278: the name-length argument is the
             * ADDRESS of this routine's own parameter slot (`pea (0xc,A6)`),
             * and the whole 0x30-byte record is filled by one call. */
            REM_NAME_$GET_ENTRY(&NAME_$CANNED_REP_ROOT_UID, name,
                                &name_len, &rep, status_ret);
            if (*status_ret != status_$ok) {
                goto cleanup;
            }

            /* Compare local vs remote UIDs */
            /* 0x00E50288-0x00E50298: `cmpm.l` over the two longwords. */
            if (local_uid_high == rep.uid.high &&
                local_uid_low == rep.uid.low) {
                /* UIDs match - check extra data (0x00E5029A). */
                if (local_extra == rep.extra) {
                    /* Entry is fully current */
                    goto cleanup;
                }
                /* Extra differs - fall through to update */
            } else {
                /* UIDs differ - check if local is a valid newer version */
                /* 0x00E502A4-0x00E502D4.  The two byte tests read the FIRST
                 * byte of each uid's high longword (`move.b (-0x50,A6)` /
                 * `move.b (-0x24,A6)`), i.e. its most significant byte. */
                if ((uint8_t)(local_uid_high >> 24) != 0 &&
                    (uint8_t)(rep.uid.high >> 24) != 0 &&
                    (local_uid_low & 0xFFFFF) == (rep.uid.low & 0xFFFFF) &&
                    local_uid_high > rep.uid.high) {
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
            /* 0x00E5031A-0x00E5033A.  link_len is 0 (`clr.w` at 0x00E50322)
             * so the link_data pointer is never dereferenced; the image's
             * `pea (-0x393c,PC)` resolves to 0x00E4C9E4, dir_$find_entry's
             * own entry point, which the compiler emitted as a dummy. */
            dir_$add_entry(local_handle, name, name_len, 3,
                           rep.extra, &rep.uid, 0,
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
        /* 0x00E50366-0x00E50382 */
        hint_data[0] = rep.extra;
        hint_data[1] = rep.uid.low & 0xFFFFF;
        HINT_$ADDI(&rep.uid, hint_data);
    }

    ACL_$EXIT_SUPER();
}

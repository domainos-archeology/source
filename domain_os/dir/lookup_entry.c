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

/*
 * DIR_$CASE_FOLD_BITMAP - 0x00E4CD84, the 12 bytes after this routine's
 * `rts' (0x00E4CD82), before dir_$get_entry_cached at 0x00E4CD90.  Three
 * readers reach it with `lea (d,PC),A0': this routine (0x00E4CCFE),
 * 0x00E4E11C and dir_$do_op_find_uid (0x00E4E692).  Image bytes
 * (`gsk read 0xE4CD84 12`): 07 ff ff fe 00 00 00 00 00 00 00 00 - the set
 * ['A'..'Z'] over chars 0..0x5F.
 */
const uint8_t DIR_$CASE_FOLD_BITMAP[DIR_CASE_FOLD_BITMAP_SIZE] = {
    0x07, 0xFF, 0xFF, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

void dir_$lookup_entry(uid_t *uid, void *name, uint16_t name_len,
                       uint16_t *type_ret, uid_t *uid_ret,
                       uint32_t *extra_ret, uint8_t *found_ret,
                       status_$t *status_ret)
{
    uint32_t local_handle;
    uint8_t *entry_ptr;
    /* A6-0x4E: dir_$find_entry's depth_ret, cleared with `clr.w (A0)`
     * at 0x00E4C9F0 - a word, not a status longword. */
    int16_t find_depth;
    uint8_t find_extra2[2];       /* A6-0x4C: dir_$find_entry's extra array */
    uint8_t find_extra3[2];       /* A6-0x4A: dir_$do_op_add_entry's result */
    status_$t add_status;         /* A6-0x40: dir_$do_op_add_entry's status */
    /*
     * A6-0x38: ONE dir_$rep_entry_t, the record REM_NAME_$GET_ENTRY fills
     * (`pea (-0x38,A6)` at 0x00E4CCBA).  Every consumer below addresses a
     * field of this record, not a separate local (source-org1):
     *   (-0x38,A6) hdr       0x00E4CCDA cmpi.w #0x1 / 0x00E4CD48 -> type_ret
     *   (-0x36,A6) name_len  0x00E4CCE4, 0x00E4CD2E
     *   (-0x34,A6) name      0x00E4CCF4 / 0x00E4CD0A (A6-0x35 + a 1-based
     *                        index), 0x00E4CD32
     *   (-0x14,A6) uid       0x00E4CD22, 0x00E4CD4C
     *   (-0x0C,A6) extra     0x00E4CD26, 0x00E4CD5E
     */
    dir_$rep_entry_t rep;

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
            /*
             * 0x00E4CC0C-0x00E4CC50: walk the one-based mount table.  The
             * count word at (0x155a,A5) is the LOW half of the longword at
             * (0x1558,A5), so it is read through DIR_MOUNT_COUNT16.  Entry n
             * lives at A5 + 0x1554 + n*8 (source uid) and A5 + 0x1594 + n*8
             * (target uid), which is what the `lea (0x1554,A0)` /
             * `lea (0x1594,A3)` pairs form.
             */
            {
                int16_t remap_count = DIR_MOUNT_COUNT16() - 1;
                if (remap_count >= 0) {
                    int16_t n = 1;
                    do {
                        if (uid_ret->high == DIR_$DATA.mount_uid[n].high &&
                            uid_ret->low  == DIR_$DATA.mount_uid[n].low) {
                            /* 0x00E4CC2E: clr.b (A1) */
                            *found_ret = 0;
                            /* 0x00E4CC42: the target uid replaces it */
                            uid_ret->high = DIR_$DATA.mount_tgt[n].high;
                            uid_ret->low  = DIR_$DATA.mount_tgt[n].low;
                            break;
                        }
                        n++;
                        remap_count--;
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

            /* 0x00E4CCB8-0x00E4CCCA: the whole 0x30-byte record is filled
             * by one call. */
            REM_NAME_$GET_ENTRY(uid, name, &name_len, &rep, status_ret);

            /* 0x00E4CCD4 / 0x00E4CCDA: both the status and rep.hdr == 1. */
            if (*status_ret == status_$ok && rep.hdr == 1) {
                /* Remote entry found - unmap case on the name in place.
                 * 0x00E4CCE4-0x00E4CD10, a 1-based `dbf` loop. */
                int16_t remaining = (int16_t)rep.name_len - 1;
                if (remaining >= 0) {
                    int16_t j = 1;
                    do {
                        uint16_t ch = (uint16_t)rep.name[j - 1];
                        /* 0x00E4CCF2-0x00E4CCFA: `sub.w D3w,D4w` sets the
                         * carry when ch > 0x5F, which skips the test. */
                        int16_t diff = (int16_t)(0x5F - ch);

                        if (ch < 0x60) {
                            /* 0x00E4CCFC-0x00E4CD02: bit (ch & 7) of
                             * DIR_$CASE_FOLD_BITMAP[(0x5F - ch) >> 3]. */
                            int16_t byte_idx = (int16_t)(diff >> 3);
                            if ((DIR_$CASE_FOLD_BITMAP[byte_idx] &
                                 (1u << (rep.name[j - 1] & 7))) != 0) {
                                /* 0x00E4CD08-0x00E4CD0A: `add.b #0x20`. */
                                rep.name[j - 1] =
                                    (uint8_t)(rep.name[j - 1] + 0x20);
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
                dir_$do_op_add_entry(uid, 0, rep.name, rep.name_len,
                                     3, rep.extra, &rep.uid, 0,
                                     (uint32_t)(uintptr_t)dir_$find_entry,
                                     find_extra3, &add_status);

                /* 0x00E4CD44-0x00E4CD5E: every result comes out of the
                 * same record. */
                *type_ret = rep.hdr;
                uid_ret->high = rep.uid.high;
                uid_ret->low  = rep.uid.low;
                *extra_ret = rep.extra;
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

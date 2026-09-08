/*
 * dir_$do_op_find_uid - DO_OP handler for find UID in directory
 *
 * Local handler for the FIND_UID operation (opcode 0x46). Opens the
 * directory, iterates through all entries looking for a match by UID.
 * Supports two modes:
 *   flag < 0: "find_net" mode - matches only low 20 bits of UID (node ID)
 *             Returns the extra field (at entry+0x0C) for type 3 entries.
 *   flag >= 0: "find_uid" mode - matches full 8-byte UID
 *             Returns the entry name.
 *
 * The search includes a UID remapping table (at A5+0x155A..0x15A0)
 * that translates "network redirect" UIDs to their local equivalents
 * before searching.
 *
 * If the entry is not found locally and the directory is NAME_$ROOT_UID,
 * falls back to REM_NAME_$FIND_NETWORK (flag<0) or REM_NAME_$FIND_UID
 * (flag>=0) to query remote nodes. On successful remote lookup, adds
 * the entry to the local directory cache via dir_$do_op_add_entry.
 * If the add fails with name_already_exists, removes and retries.
 *
 * Entry name case folding (uppercase to lowercase) is applied to remote
 * results using the DIR_$CASE_FOLD_BITMAP bitmap.
 *
 * Parameters:
 *   uid        - Directory UID to search
 *   target_uid - UID to find (pointer)
 *   flag       - Search mode: <0 = find_net, >=0 = find_uid
 *   name_ret   - Output: pointer to name buffer
 *   len_ret    - Output: name length
 *   extra_ret  - Output: extra data (node ID for find_net)
 *   status_ret - Output: status code
 *
 * Original address: 0x00E4E41A
 * Original size: 876 bytes
 */

#include "dir/dir_internal.h"

/* REM_NAME_$FIND_UID and REM_NAME_$FIND_NETWORK are declared in
 * name/name.h (included via dir_internal.h -> name/name.h).
 * Canonical signatures:
 *   void REM_NAME_$FIND_UID(uid_t *dir_uid, uid_t *target_uid,
 *                           void *entry_ret, status_$t *status_ret);
 *   void REM_NAME_$FIND_NETWORK(uid_t *dir_uid, uint32_t *target_node,
 *                               void *entry_ret, status_$t *status_ret);
 */

/* Name offset table */

/* Case folding bitmap */

/* DIR_$READU_NUL_NAME - NUL byte name for dir_$find_entry */

void dir_$do_op_find_uid(uid_t *uid, uid_t *target_uid, int8_t flag,
                         void *name_ret, void *len_ret, void *uid_ret,
                         status_$t *status_ret)
{
    /* Cast void* parameters to typed pointers for internal use */
    int32_t name_buf = (int32_t)(uintptr_t)name_ret;
    uint16_t *name_len_ret = (uint16_t *)len_ret;
    uint32_t *extra_ret = (uint32_t *)uid_ret;

    char *blk = DIR_$BLOCK;   /* the routine's own A5 = 0x00E7DC00 */
    uint32_t local_handle[4];
    uint32_t search_uid_high;
    uint32_t search_uid_low;
    int8_t found;
    uint8_t *entry;
    uint8_t *page_data;
    int16_t depth;
    uint16_t page_idx;
    int16_t base_offset;
    int16_t num_entries;
    int16_t *offset_array;
    uint16_t extra_array[18];
    void *find_ret;
    status_$t local_status;

    /* A6-0x48: the ONE record REM_NAME_$FIND_UID / REM_NAME_$FIND_NETWORK
     * fill in (`pea (-0x48,A6)` at 0x00E4E632 and 0x00E4E65A). */
    dir_$find_uid_result_t result;
    /* A6-0x86: a two-byte cell of its own, dir_$do_op_add_entry's tenth
     * argument (`pea (-0x86,A6)` at 0x00E4E6AC and 0x00E4E706). */
    uint8_t add_entry_arg10[2];
    uint8_t drop_buf[16];

    ACL_$ENTER_SUPER();

    *name_len_ret = 0;
    *extra_ret = 0;

    /* Open directory with read access, no ACL right */
    dir_$open_dir(uid, 1, 0, local_handle, status_ret);
    if (*status_ret != status_$ok) goto done;

    /* Copy target UID, checking the remap table */
    search_uid_high = target_uid->high;
    search_uid_low = target_uid->low;

    {
        /* The mount tables are ONE-BASED - see DIR_MOUNT_UID_TAB_OFF. */
        int16_t remaining =
            (int16_t)(DIR_MOUNT_COUNT16(blk) - 1);
        int16_t n;

        for (n = 1; remaining >= 0; n++, remaining--) {
            if (target_uid->high ==
                    DIR_MOUNT_TGT_OF(blk, n).high &&
                target_uid->low ==
                    DIR_MOUNT_TGT_OF(blk, n).low) {
                search_uid_high =
                    DIR_MOUNT_UID_OF(blk, n).high;
                search_uid_low =
                    DIR_MOUNT_UID_OF(blk, n).low;
                break;
            }
        }
    }

    /* Find the first entry in the B-tree */
    dir_$find_entry((void *)local_handle[0], &DIR_$READU_NUL_NAME, 1,
                    0x20, &find_ret, extra_array + 2, &depth);

    /* Map the starting page */
    page_data = (uint8_t *)dir_$map_page((void *)local_handle[0],
        extra_array[depth * 2]);

    found = 0;
    entry = (uint8_t *)find_ret;

    /* Iterate through all entries */
    for (;;) {
        /* Compute entry count on current page */
        if (*(int16_t *)(page_data + 10) == 0) {
            base_offset = *(int16_t *)(page_data + 0x14) + 0x12;
        } else {
            base_offset = 0x12;
        }
        offset_array = (int16_t *)(page_data + base_offset);

        {
            int iVar = (int)(*(int16_t *)(page_data + 0x0E)) - (int)base_offset;
            if (iVar < 0) iVar += 1;
            num_entries = (int16_t)(iVar >> 1) - 1;
        }

        if ((int32_t)((uint32_t)(uint16_t)num_entries << 16) >= 0) {
            do {
                entry = (uint8_t *)((int32_t)page_data + *offset_array);

                if ((*entry & 7) == 2 || (*entry & 7) == 3) {
                    int match;
                    if (flag < 0) {
                        /* find_net: match low 20 bits only */
                        match = ((search_uid_low & 0xFFFFF) ==
                                 (*(uint32_t *)(entry + 8) & 0xFFFFF));
                    } else {
                        /* find_uid: match full UID */
                        match = (search_uid_high == *(uint32_t *)(entry + 4) &&
                                 search_uid_low == *(uint32_t *)(entry + 8));
                    }
                    found = -(int8_t)match;
                    if (found < 0) goto found_entry;
                }

                num_entries--;
                offset_array++;
            } while (num_entries != -1);
        }

        if (found < 0) goto found_entry;

        /* Move to next page */
        dir_$next_page((void *)local_handle[0], depth, extra_array + 2,
                       &page_idx);
        if (page_idx == (uint16_t)-1) break;
        page_data = (uint8_t *)dir_$map_page((void *)local_handle[0], page_idx);
    }

    if (found < 0) {
found_entry:
        if (flag < 0) {
            /* find_net: return extra field for type 3 entries */
            if ((*entry & 7) == 3) {
                *extra_ret = *(uint32_t *)(entry + 0x0C);
            }
        } else {
            /* find_uid: return the entry name */
            int16_t name_offset = DIR_$NAME_OFFSET_TABLE[(*entry & 7)];
            uint8_t *ename = entry + name_offset;
            uint8_t nlen = entry[1];
            *name_len_ret = (uint16_t)nlen;

            int16_t k = nlen - 1;
            if (k >= 0) {
                int16_t j = 1;
                do {
                    *(uint8_t *)(name_buf + j - 1) = ename[j - 1];
                    j++;
                    k--;
                } while (k != -1);
            }
        }
    } else {
        /* Not found locally - check if this is root directory */
        if (uid->high == NAME_$ROOT_UID.high &&
            uid->low == NAME_$ROOT_UID.low) {

            /* Release local handle before remote call */
            dir_$release_handle(local_handle);

            if (flag < 0) {
                /* find_net: query remote nodes */
                uint32_t net_mask = target_uid->low & 0xFFFFF;
                REM_NAME_$FIND_NETWORK(uid, &net_mask,
                                       &result, status_ret);
            } else {
                /* find_uid: query remote */
                REM_NAME_$FIND_UID(uid, target_uid, &result, status_ret);
            }

            if (*status_ret == status_$ok) {
                /* Case-fold the returned name (uppercase -> lowercase) */
                {
                    uint16_t ci = result.name_len - 1;
                    if ((int32_t)((uint32_t)ci << 16) >= 0) {
                        int16_t j = 1;
                        do {
                            uint8_t ch = result.name[j - 1];
                            uint16_t char_val = (uint16_t)ch;
                            int16_t offset = 0x5F - char_val;

                            if (char_val < 0x60 && offset >= 0) {
                                uint16_t byte_idx = (uint16_t)offset >> 3;
                                if ((*(&DIR_$CASE_FOLD_BITMAP + byte_idx) &
                                    (1 << (ch & 7))) != 0) {
                                    result.name[j - 1] = ch + 0x20;
                                }
                            }
                            j++;
                            ci--;
                        } while (ci != 0xFFFF);
                    }
                }

                /* 0x00E4E6A8-0x00E4E6CE: eleven arguments; the tenth is the
                 * A6-0x86 cell, NOT the REM_NAME result record. */
                dir_$do_op_add_entry(uid, 0, result.name, result.name_len,
                                     3, result.node_id, result.extra,
                                     0, (uint32_t)(uintptr_t)dir_$find_entry,
                                     add_entry_arg10, &local_status);

                if (local_status == status_$name_already_exists) {
                    /* Remove stale entry and retry */
                    dir_$do_op_drop_entry(uid, 0, result.name, result.name_len,
                                          3, drop_buf, &local_status);
                    dir_$do_op_add_entry(uid, 0, result.name, result.name_len,
                                         3, result.node_id, result.extra,
                                         0, (uint32_t)(uintptr_t)dir_$find_entry,
                                         add_entry_arg10, &local_status);
                }

                /* Return results */
                if (flag < 0) {
                    *extra_ret = result.node_id;
                } else {
                    *name_len_ret = result.name_len;
                    uint16_t ci = *name_len_ret - 1;
                    if ((int32_t)((uint32_t)ci << 16) >= 0) {
                        int16_t j = 1;
                        do {
                            *(uint8_t *)(name_buf + j - 1) = result.name[j - 1];
                            j++;
                            ci--;
                        } while (ci != 0xFFFF);
                    }
                }
                goto done;
            }
        }

        /* Not found */
        *status_ret = status_$naming_name_not_found;
    }

done:
    dir_$release_handle(local_handle);
    ACL_$EXIT_SUPER();
}

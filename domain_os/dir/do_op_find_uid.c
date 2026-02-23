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
 * results using the PTR_DAT_00e4cd84 bitmap.
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

/* REM_NAME_$FIND_UID - Remote find UID */
extern void REM_NAME_$FIND_UID(uid_t *dir_uid, uid_t *target_uid,
                                void *result_buf, status_$t *status_ret);

/* REM_NAME_$FIND_NETWORK - Remote find network node */
extern void REM_NAME_$FIND_NETWORK(uid_t *dir_uid, void *net_id,
                                    void *result_buf, status_$t *status_ret);

/* Name offset table */
extern int16_t DIR_$NAME_OFFSET_TABLE[];

/* Case folding bitmap */
extern uint8_t PTR_DAT_00e4cd84;

/* DAT_00e4dffc - NUL byte name for dir_$find_entry */
extern uint8_t DAT_00e4dffc;

void dir_$do_op_find_uid(uid_t *uid, uid_t *target_uid, int8_t flag,
                         int32_t name_buf, uint16_t *name_len_ret,
                         uint32_t *extra_ret, status_$t *status_ret)
{
    char *a5 = (char *)__A5_BASE();
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

    /* Remote lookup result buffer */
    uint8_t result_buf[2];
    uint16_t result_name_len;
    uint8_t result_name[32];
    uint8_t result_extra[8];
    uint32_t result_node_id;
    uint32_t result_uid_high;
    uint32_t result_uid_low;
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
        uint16_t remap_count = *(int16_t *)(a5 + 0x155A) - 1;
        if ((int32_t)((uint32_t)remap_count << 16) >= 0) {
            int16_t ri = 1;
            char *remap_base = a5;
            do {
                if (target_uid->high == *(uint32_t *)(remap_base + 8 + 0x1594) &&
                    target_uid->low == *(uint32_t *)(remap_base + 8 + 0x1598)) {
                    char *slot = a5 + ri * 8;
                    search_uid_high = *(uint32_t *)(slot + 0x1554);
                    search_uid_low = *(uint32_t *)(slot + 0x1558);
                    break;
                }
                ri++;
                remap_count--;
                remap_base += 8;
            } while (remap_count != 0xFFFF);
        }
    }

    /* Find the first entry in the B-tree */
    dir_$find_entry((void *)local_handle[0], &DAT_00e4dffc, 1,
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
                                       result_buf, status_ret);
            } else {
                /* find_uid: query remote */
                REM_NAME_$FIND_UID(uid, target_uid, result_buf, status_ret);
            }

            if (*status_ret == status_$ok) {
                /* Case-fold the returned name (uppercase -> lowercase) */
                {
                    uint16_t ci = result_name_len - 1;
                    if ((int32_t)((uint32_t)ci << 16) >= 0) {
                        int16_t j = 1;
                        do {
                            uint8_t ch = result_name[j - 1];
                            uint16_t char_val = (uint16_t)ch;
                            int16_t offset = 0x5F - char_val;

                            if (char_val < 0x60 && offset >= 0) {
                                uint16_t byte_idx = (uint16_t)offset >> 3;
                                if ((*(&PTR_DAT_00e4cd84 + byte_idx) &
                                    (1 << (ch & 7))) != 0) {
                                    result_name[j - 1] = ch + 0x20;
                                }
                            }
                            j++;
                            ci--;
                        } while (ci != 0xFFFF);
                    }
                }

                /* Add entry to local directory cache */
                dir_$do_op_add_entry(uid, 0, result_name, result_name_len,
                                     3, result_node_id, result_extra,
                                     0, (uint32_t)(uintptr_t)dir_$find_entry,
                                     result_buf, &local_status);

                if (local_status == status_$name_already_exists) {
                    /* Remove stale entry and retry */
                    dir_$do_op_drop_entry(uid, 0, result_name, result_name_len,
                                          3, drop_buf, &local_status);
                    dir_$do_op_add_entry(uid, 0, result_name, result_name_len,
                                         3, result_node_id, result_extra,
                                         0, (uint32_t)(uintptr_t)dir_$find_entry,
                                         result_buf, &local_status);
                }

                /* Return results */
                if (flag < 0) {
                    *extra_ret = result_node_id;
                } else {
                    *name_len_ret = result_name_len;
                    uint16_t ci = *name_len_ret - 1;
                    if ((int32_t)((uint32_t)ci << 16) >= 0) {
                        int16_t j = 1;
                        do {
                            *(uint8_t *)(name_buf + j - 1) = result_name[j - 1];
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

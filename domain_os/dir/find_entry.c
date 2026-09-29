/*
 * dir_$find_entry - Search for a named entry in a directory page tree
 *
 * Performs a binary search within directory pages to find an entry by name.
 * Handles both leaf pages (entry_type == 0) and B-tree interior pages
 * (entry_type == 1) by descending through the tree structure.
 *
 * The directory is organized as a B-tree where each page has a sorted
 * array of entry offset indices. The binary search compares entry names
 * using a type-dependent name offset table at (A5 + 0x2000 + type*2).
 *
 * Parameters:
 *   handle    - Directory handle (passed to dir_$map_page)
 *   name      - Entry name to search for
 *   name_len  - Length of name
 *   flags     - Search flags (0=normal lookup, 0x20=for add/B-tree traversal)
 *   entry_ret - Output: pointer to found entry data
 *   extra     - Output: page/position tracking array (4 bytes per level)
 *               Each entry: [page_self_index (2 bytes), slot_index (2 bytes)]
 *   depth_ret - Output: pointer to depth counter (number of levels traversed)
 *
 * Returns:
 *   Negative (char < 0) if found (exact match)
 *   Zero or positive if not found
 *   Special case: name_len==1 && name[0]=='\0' always returns 0 (not found)
 *
 * Original address: 0x00E4C9E4
 * Original size: 390 bytes
 */

#include "dir/dir_internal.h"

/* Name offset table: for each entry type (0-7), gives offset from entry
 * base to where the name starts. Located at A5+0x2000 on M68K. */

char dir_$find_entry(void *handle, void *name, int16_t name_len,
                     int16_t flags, void **entry_ret,
                     void *extra, int16_t *depth_ret)
{
    uint8_t *page_data;
    int16_t base_offset;
    int16_t num_entries;
    int16_t lo, hi;
    int16_t mid;
    int16_t cmp_result;
    uint8_t *entry;
    int16_t entry_type;
    int16_t entry_name_len;
    int16_t name_offset;
    int16_t i;
    int iVar;
    uint8_t *name_bytes = (uint8_t *)name;
    int16_t *offset_array;
    uint16_t *extra_array = (uint16_t *)extra;

    *depth_ret = 0;

    /* Map page 0 */
    page_data = (uint8_t *)dir_$map_page(handle, 0);

    /* Compute base offset to index array */
    base_offset = *(int16_t *)(page_data + 0x14) + 0x12;

    for (;;) {
        /* Compute number of entries on this page */
        iVar = (int)(*(int16_t *)(page_data + 0x0E)) - (int)base_offset;
        if (iVar < 0) {
            iVar = iVar + 1;
        }
        num_entries = (int16_t)(iVar >> 1);

        /* Check page entry type */
        entry_type = (*page_data >> 6);
        if (entry_type == 1) {
            /* B-tree interior page: search starts at index 2 */
            lo = 2;
            cmp_result = -1;
            mid = 2;
        } else {
            /* Leaf page */
            lo = 1;
        }

        /* Binary search within the page */
        while (lo <= num_entries) {
            mid = (lo + num_entries);
            if (mid < 0) {
                mid = mid + 1;
            }
            mid = mid >> 1;

            /* Get entry pointer from offset array */
            offset_array = (int16_t *)(page_data + base_offset);
            entry = page_data + *(int16_t *)((uint8_t *)offset_array + mid * 2 - 2);
            *entry_ret = entry;

            /* Get entry's name offset based on type */
            name_offset = DIR_$DATA.name_offset_table[(*entry & 7)];
            entry_name_len = (int16_t)(uint8_t)entry[1];

            /* Compare names byte by byte */
            i = 1;
            for (;;) {
                if (i > name_len) {
                    /* Searched name exhausted - check if entry name is longer */
                    if (i <= entry_name_len) {
                        /* Entry name is longer: searched name < entry name */
                        cmp_result = -1;
                        num_entries = mid - 1;
                        goto next_iter;
                    }
                    /* Entry name same length: exact match */
                    cmp_result = 0;
                    goto done_search;
                }
                if (i > entry_name_len) {
                    /* Entry name exhausted but searched name continues:
                     * searched name > entry name */
                    break;
                }
                /* Compare bytes (unsigned) */
                if ((uint8_t)name_bytes[i - 1] < entry[i + name_offset - 1]) {
                    /* searched < entry */
                    cmp_result = -1;
                    num_entries = mid - 1;
                    goto next_iter;
                }
                if ((uint8_t)name_bytes[i - 1] > entry[i + name_offset - 1]) {
                    break;
                }
                i++;
            }
            /* searched name > entry name */
            cmp_result = 1;
            lo = mid + 1;
next_iter:
            continue;
        }

        /* End of binary search */
        if (cmp_result < 0) {
            mid = mid - 1;
        }

done_search:
        /* Record this level's page and position */
        *depth_ret = *depth_ret + 1;
        if (*depth_ret > flags) {
            if (flags != 0) {
                CRASH_SYSTEM(&Naming_bad_request_header_ver_err);
            }
        } else {
            int16_t slot = *depth_ret << 2;
            /* Store page_self_index and slot position */
            *(uint16_t *)((uint8_t *)extra + slot - 4) = *(uint16_t *)(page_data + 10);
            *(int16_t *)((uint8_t *)extra + slot - 2) = mid;
        }

        /* Check page type for tree descent */
        if ((*page_data >> 6) != 1) {
            /* Leaf page - done */
            if (name_len == 1 && name_bytes[0] == '\0') {
                return 0;
            } else {
                return -(cmp_result == 0);
            }
        }

        /* Interior page - descend to child.
         *
         * 0x00E4CB1A-0x00E4CB44.  `moveq #0x12,D3` at 0x00E4CB1C sets the
         * base offset for the NEXT iteration - only the root page has the
         * variable-length area whose length lives at page+0x14, so every
         * page below it indexes from a plain 0x12.  The entry that is read
         * here still comes from A3, i.e. from the CURRENT page's base
         * offset (`move.w (-0x2,A3,D0*0x1),D0w` at 0x00E4CB26), which is
         * why the assignment must not be folded into the read. */
        {
            int16_t child_offset;

            offset_array = (int16_t *)(page_data + base_offset);
            child_offset = *(int16_t *)((uint8_t *)offset_array + mid * 2 - 2);
            base_offset = 0x12;
            *entry_ret = page_data + child_offset;
            page_data = (uint8_t *)dir_$map_page(handle,
                *(uint16_t *)(page_data + child_offset + 2));
        }
    }
}

/*
 * dir_$refind_entry - Re-find current position after page processing
 *
 * Originally a nested Pascal subprocedure of dir_$do_op_dir_readu. Called
 * when depth==0 after processing all entries on a page, before calling
 * dir_$next_page. This function re-establishes the B-tree position by
 * looking up the last entry that was processed.
 *
 * If num_entries is 0 (empty page), sets the eof_ret flag and returns
 * immediately. Otherwise, it:
 *   1. Reads the index table to find the last entry's page offset
 *   2. Copies the entry's name to a local buffer
 *   3. Calls dir_$find_entry to re-locate the position in the B-tree
 *   4. Crashes if the entry is not found (it should always exist)
 *
 * Parameters:
 *   local_handle   - Directory handle
 *   page_data      - Mapped page data (parent's -0xF8)
 *   idx_base       - Start of the index table (parent's -0xF4)
 *   entry_ptr_ret  - Output: entry pointer (parent's -0xF0)
 *   entry_name_ret - Output: entry name pointer (parent's -0xEC)
 *   extra_array    - Extra array for dir_$find_entry (parent's -0xD8)
 *   depth_ret      - Output: depth from dir_$find_entry (parent's -0x10A)
 *   num_entries    - Number of entries on current page (parent's -0x10C)
 *   eof_ret        - Output: EOF flag, set to 0xFF if empty (parent's -0x114)
 *
 * Original address: 0x00E4D8AA
 * Original size: 170 bytes
 */

#include "dir/dir_internal.h"

/* Name offset table at A5+0x2000, indexed by entry type & 7 */
extern int16_t DIR_$NAME_OFFSET_TABLE[];

void dir_$refind_entry(uint32_t local_handle, uint8_t *page_data,
                       uint8_t *idx_base, void **entry_ptr_ret,
                       void **entry_name_ret, void *extra_array,
                       int16_t *depth_ret, int16_t num_entries,
                       int8_t *eof_ret)
{
    uint8_t name_buf[265];

    if (num_entries == 0) {
        /* No entries on this page - signal EOF */
        *eof_ret = (int8_t)0xFF;
        return;
    }

    /* Get the offset of the last entry from the index table.
     * The index table at idx_base is an array of int16_t offsets
     * from page_data to each entry. */
    {
        int16_t *idx = (int16_t *)idx_base;
        int16_t entry_offset = idx[num_entries - 1];
        uint8_t *entry = page_data + (int32_t)entry_offset;
        *entry_ptr_ret = entry;

        /* Compute name pointer using name offset table */
        uint8_t *entry_name = entry + DIR_$NAME_OFFSET_TABLE[*entry & 7];
        *entry_name_ret = entry_name;

        /* Get name length from entry byte 1 */
        uint16_t name_len = (uint16_t)(uint8_t)entry[1];

        /* Copy name to local buffer */
        int16_t remaining = (int16_t)name_len - 1;
        if (remaining >= 0) {
            int16_t j = 1;
            do {
                name_buf[j] = entry_name[j - 1];
                j++;
                remaining--;
            } while (remaining != -1);
        }

        /* Re-find entry in the B-tree */
        char found = dir_$find_entry((void *)(uintptr_t)local_handle,
                                     name_buf + 1, name_len, 0x20,
                                     entry_ptr_ret, extra_array, depth_ret);
        if (found >= 0) {
            /* Entry should always be found - crash if not */
            CRASH_SYSTEM((const status_$t *)&Naming_bad_request_header_ver_err);
        }
    }
}

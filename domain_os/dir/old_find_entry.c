/*
 * dir_$old_find_entry - Find entry in directory by name
 *
 * Searches a directory for a named entry. The directory structure
 * uses a combination of inline entries and hash-chained overflow
 * entries.
 *
 * Search process:
 * 1. Check if directory has any entries (entry_count at offset 0x16)
 * 2. First, search inline entries (slots 1..inline_count at 0x30 intervals)
 *    - Each entry has: name at relative offset -0x16, length at +0x10/+0x11
 *    - Compare using NAMEQ (case-insensitive name match)
 * 3. If not found inline, compute hash of the name (dir_$old_hash_name)
 *    - Look up the hash chain head at offset 0x3AA + hash*2
 * 4. Walk the hash chain following overflow entries:
 *    - Overflow entries are at handle + slot*0x96
 *    - Each has secondary entries at 0x30 intervals
 *    - Chain link at offset 0x36A in each overflow block
 * 5. Safety limit: abort after 0x514 chain hops
 *
 * Parameters:
 *   handle      - Directory handle (pointer to directory data)
 *   name        - Name to search for
 *   name_len    - Length of name
 *   entry_ret   - Output: pointer to found entry data
 *   slot_idx    - Output: slot index where entry was found
 *   chain_level - Output: 0 if found in inline area, >0 if in overflow
 *
 * Returns:
 *   0xFF (true/negative) if entry found, 0 (false) if not found
 *
 * Original address: 0x00E54B9E
 * Size: 368 bytes
 */

#include "dir/dir_internal.h"

int8_t dir_$old_find_entry(uint32_t handle, uint8_t *name, uint16_t name_len,
                           int32_t *entry_ret, uint16_t *slot_idx,
                           uint16_t *chain_level)
{
    int16_t count;
    uint16_t i;
    uint16_t hash_result;
    uint16_t chain_hops;
    char *dir_base = (char *)(uintptr_t)handle;

    *entry_ret = 0;

    /* Check if directory has any entries */
    if (*(int16_t *)(dir_base + 0x16) == 0) {
        return 0;
    }

    /* Search inline entries first */
    if (*(int16_t *)(dir_base + 0x04) != 0) {
        count = *(int16_t *)(dir_base + 0x04) - 1;
        i = 1;
        char *entry = dir_base + 0x30;

        do {
            /* Check if entry has a name (length byte at offset 0x11 is non-zero) */
            if (*(char *)(entry + 0x11) != 0) {
                uint16_t entry_name_len = (uint16_t)(uint8_t)*(entry + 0x10);
                /* Compare names using NAMEQ */
                if (NAMEQ(name, &name_len, entry - 0x16 + 0x30, &entry_name_len) < 0) {
                    /* Found in inline area */
                    *chain_level = 0;
                    *slot_idx = i;
                    *entry_ret = (int32_t)(uintptr_t)(dir_base - 0x16 + (int16_t)(*slot_idx * 0x30));
                    return (int8_t)0xFF;
                }
            }
            i++;
            entry += 0x30;
            count--;
        } while (count != -1);
    }

    /* Hash lookup for overflow entries */
    hash_result = dir_$old_hash_name(name, name_len, *(uint16_t *)(dir_base + 0x02));
    *slot_idx = *(uint16_t *)(dir_base + (hash_result & 0xFFFF) * 2 + 0x3AA);
    chain_hops = 0;

    while (*slot_idx != 0) {
        char *overflow_base = dir_base + (uint32_t)*slot_idx * 0x96;
        chain_hops++;

        /* Safety limit to prevent infinite loops */
        if (chain_hops > 0x514) {
            return 0;
        }

        /* Search secondary entries within this overflow block */
        if (*(int16_t *)(dir_base + 0x08) != 0) {
            int16_t sec_count = *(int16_t *)(dir_base + 0x08) - 1;
            int16_t sec_idx = 1;
            int32_t offset = 0x30;

            do {
                char *sec_entry = overflow_base + offset;

                /* Check if secondary entry has a name */
                if (*(char *)(sec_entry + 0x367) != 0) {
                    uint16_t sec_name_len = (uint16_t)(uint8_t)*(sec_entry + 0x366);
                    /* Compare names */
                    if (NAMEQ(name, &name_len, sec_entry + 0x340, &sec_name_len) < 0) {
                        /* Found in overflow chain */
                        *chain_level = sec_idx;
                        *entry_ret = (int32_t)(uintptr_t)(overflow_base +
                                     (int16_t)(*chain_level * 0x30) + 0x340);
                        return (int8_t)0xFF;
                    }
                }
                sec_idx++;
                offset += 0x30;
                sec_count--;
            } while (sec_count != -1);
        }

        /* Follow chain link to next overflow block */
        *slot_idx = *(uint16_t *)(overflow_base + 0x36A);
    }

    return 0;
}

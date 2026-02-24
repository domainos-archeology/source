/*
 * dir_$old_add_entry - Add entry to old-format directory buffer
 *
 * Core directory entry addition function. Checks for duplicate names,
 * then adds the entry to either the flat (inline) entry area or the
 * hashed overflow area.
 *
 * Process:
 * 1. Clear *result output pointer
 * 2. Call dir_$old_find_entry to check if name already exists
 *    - If found: return status_$name_already_exists (0xE0003)
 * 3. If flags >= 0 and entry_count == max inline entries, fail
 * 4. Try inline entry area first via dir_$old_find_free_inline_slot:
 *    - Entry at handle + slot * 0x30, stride 0x30 (48 bytes)
 *    - Copy name (space-padded to 32 chars)
 *    - Set entry type byte and 8-byte UID
 *    - Increment entry count at handle + 0x16
 *    - Return pointer to new entry in *result
 * 5. If inline area full, try hashed overflow area:
 *    - Hash the name via dir_$old_hash_name
 *    - Find/allocate overflow slot via dir_$old_find_overflow_slot
 *    - Overflow entries at handle + bucket*0x96 + sub_slot*0x30
 *    - Same entry layout, offset by 0x340
 * 6. If no space found: return status_$directory_is_full (0xE0002)
 *
 * Parameters:
 *   dir_uid    - UID of directory (passed through, not used directly)
 *   handle     - Mapped directory buffer handle
 *   name       - Entry name (1-indexed Pascal string)
 *   name_len   - Length of name
 *   type       - Entry type code
 *   uid_data   - Pointer to 8-byte UID to store in entry
 *   flags      - Operation flags (high byte: negative = replace mode)
 *   result     - Output: pointer to new entry (written as int32_t*)
 *   status_ret - Output: status code
 *
 * Original address: 0x00E55220
 * Size: 486 bytes
 */

#include "dir/dir_internal.h"

void dir_$old_add_entry(uid_t *dir_uid, uint32_t handle, uint8_t *name,
                        uint16_t name_len, uint16_t type, void *uid_data,
                        uint16_t flags, uint8_t *result, status_$t *status_ret)
{
    char *dir_base = (char *)(uintptr_t)handle;
    uint32_t *uid_ptr = (uint32_t *)uid_data;
    int8_t found;
    uint16_t slot_idx;
    uint16_t chain_level;
    uint16_t hash;
    int8_t flag_byte = (int8_t)(flags >> 8);  /* high byte of flags word */

    /* Clear result pointer */
    *(int32_t *)result = 0;

    /* Check if name already exists */
    found = dir_$old_find_entry(handle, name, name_len,
                                (int32_t *)result, &slot_idx, &chain_level);
    if (found < 0) {
        *status_ret = status_$name_already_exists;
        return;
    }

    /* Check if we can add: either replace mode (flags < 0) or
     * there's room (entry_count != max_inline_count) */
    if (flag_byte >= 0 &&
        *(int16_t *)(dir_base + 0x16) == *(int16_t *)(dir_base + 0x18)) {
        *status_ret = status_$directory_is_full;
        return;
    }

    /* Try inline entry area first */
    found = dir_$old_find_free_inline_slot(handle,
                *(uint16_t *)(dir_base + 0x04), &slot_idx);

    if (found < 0) {
        /* Found free inline slot - populate it */
        char *entry = dir_base + (uint32_t)slot_idx * 0x30;
        uint16_t i;

        /* Set entry type byte */
        *(uint8_t *)(entry + 0x11) = (uint8_t)type;

        /* Copy 8-byte UID */
        *(uint32_t *)(entry + 0x12) = uid_ptr[0];
        *(uint32_t *)(entry + 0x16) = uid_ptr[1];

        /* Copy name bytes (1-indexed source) */
        if (name_len != 0) {
            int16_t count = name_len - 1;
            i = 1;
            do {
                *(uint8_t *)(entry - 0x17 + (uint32_t)i) =
                    *(uint8_t *)(name - 1 + (uint32_t)i);
                i++;
                count--;
            } while (count != -1);
        }

        /* Space-pad remaining name bytes up to 32 */
        i = name_len + 1;
        if (i < 0x21) {
            int16_t pad_count = 0x20 - i;
            do {
                *(uint8_t *)(entry - 0x17 + (uint32_t)i) = 0x20;
                i++;
                pad_count--;
            } while (pad_count != -1);
        }

        /* Store name length byte */
        *(uint8_t *)(entry + 0x10) = (uint8_t)name_len;

        /* Success */
        *status_ret = status_$ok;

        /* Increment entry count */
        *(int16_t *)(dir_base + 0x16) += 1;

        /* Return pointer to entry */
        *(int32_t *)result = (int32_t)(uintptr_t)(dir_base - 0x16 +
                              (uint32_t)slot_idx * 0x30);
        return;
    }

    /* Inline area full - try overflow area */
    hash = dir_$old_hash_name(name, name_len, *(uint16_t *)(dir_base + 0x02));

    found = dir_$old_find_overflow_slot(handle, hash, flag_byte,
                                         &slot_idx, &chain_level);

    if (found < 0) {
        /* Found overflow slot - populate it */
        char *bucket = dir_base + (uint32_t)slot_idx * 0x96;
        char *sub_entry = bucket + (uint32_t)chain_level * 0x30;
        uint16_t i;

        /* Set entry type byte */
        *(uint8_t *)(sub_entry + 0x367) = (uint8_t)type;

        /* Increment chain entry count */
        *(uint8_t *)(bucket + 0x36e) += 1;

        /* Copy 8-byte UID */
        *(uint32_t *)(sub_entry + 0x368) = uid_ptr[0];
        *(uint32_t *)(sub_entry + 0x36c) = uid_ptr[1];

        /* Copy name bytes (1-indexed source) */
        if (name_len != 0) {
            int16_t count = name_len - 1;
            i = 1;
            do {
                *(uint8_t *)(sub_entry + (uint32_t)i + 0x33f) =
                    *(uint8_t *)(name - 1 + (uint32_t)i);
                i++;
                count--;
            } while (count != -1);
        }

        /* Space-pad remaining name bytes up to 32 */
        i = name_len + 1;
        if (i < 0x21) {
            int16_t pad_count = 0x20 - i;
            do {
                *(uint8_t *)(sub_entry + (uint32_t)i + 0x33f) = 0x20;
                i++;
                pad_count--;
            } while (pad_count != -1);
        }

        /* Store name length byte */
        *(uint8_t *)(sub_entry + 0x366) = (uint8_t)name_len;

        /* Success */
        *status_ret = status_$ok;

        /* Increment entry count */
        *(int16_t *)(dir_base + 0x16) += 1;

        /* Return pointer to overflow entry */
        *(int32_t *)result = (int32_t)(uintptr_t)(sub_entry + 0x340);
        return;
    }

    /* No space found */
    *status_ret = status_$directory_is_full;
}

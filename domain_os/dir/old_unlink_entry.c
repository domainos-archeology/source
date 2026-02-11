/*
 * dir_$old_unlink_entry - Find and remove directory entry by name
 *
 * Finds the named entry via dir_$old_find_entry, checks its type
 * (0=file, 1=hard link, 3=soft link), copies UID to result (or
 * UID_$NIL for type 0/3), then removes via dir_$old_delete_entry.
 *
 * Type checking:
 *   type 0: copies UID_$NIL to result
 *   type 1: copies entry UID to result; if op_type==3, sets
 *           status_$naming_not_a_link (but still removes)
 *   type 3: copies UID_$NIL to result; if op_type==1, sets
 *           status_$naming_invalid_link_operation and aborts
 *   other:  no UID copy, no removal (jumps to status check)
 *
 * Parameters:
 *   dir_uid  - UID of the directory (used as buffer base via handle)
 *   handle   - Mapped directory buffer base
 *   name     - Entry name to find and remove
 *   name_len - Length of name
 *   op_type  - Operation type (1=drop hard link, 3=drop soft link)
 *   result   - Output: 8-byte UID of removed entry
 *   status_ret - Output: status code
 *
 * Original address: 0x00E5569C
 * Size: 200 bytes
 */

#include "dir/dir_internal.h"

void dir_$old_unlink_entry(uid_t *dir_uid, uint32_t handle, uint8_t *name,
                           uint16_t name_len, uint16_t op_type,
                           void *result, status_$t *status_ret)
{
    int8_t found;
    int32_t entry_ptr;
    uint16_t slot_idx;
    uint16_t chain_level;
    uint16_t hash;
    uint8_t entry_type;
    uid_t *uid_src;

    *status_ret = status_$ok;

    /* Find the entry by name */
    found = dir_$old_find_entry(handle, name, name_len,
                                &entry_ptr, &slot_idx, &chain_level);
    if (found >= 0) {
        /* Entry not found */
        *status_ret = status_$naming_name_not_found;
        goto check_remove;
    }

    /* Check entry type at offset 0x27 */
    entry_type = *(uint8_t *)((char *)(uintptr_t)entry_ptr + 0x27);

    switch (entry_type) {
        case 0:
            /* File entry (type 0) - return NIL UID */
            uid_src = &UID_$NIL;
            break;

        case 1:
            /* Hard link entry (type 1) - return entry's UID */
            if (op_type == 3) {
                /* Trying to drop soft link but this is a hard link */
                *status_ret = status_$naming_not_a_link;
            }
            uid_src = (uid_t *)((char *)(uintptr_t)entry_ptr + 0x28);
            break;

        case 3:
            /* Soft link entry (type 3) */
            if (op_type == 1) {
                /* Trying to drop hard link but this is a soft link */
                *status_ret = status_$naming_invalid_link_operation;
                goto check_remove;
            }
            uid_src = &UID_$NIL;
            break;

        default:
            /* Unknown type - skip to status check */
            goto check_remove;
    }

    /* Copy UID to result (8 bytes) */
    ((uid_t *)result)->high = uid_src->high;
    ((uid_t *)result)->low = uid_src->low;

check_remove:
    /* If status is still OK, compute hash and delete the entry */
    if ((int16_t)*status_ret == 0) {
        hash = dir_$old_hash_name(name, name_len,
                                  *(uint16_t *)((char *)(uintptr_t)handle + 2));
        dir_$old_delete_entry(handle, slot_idx, chain_level, hash);
    }
}

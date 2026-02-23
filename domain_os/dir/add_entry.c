/*
 * dir_$add_entry - Core internal add entry to directory
 *
 * Validates the entry name (no NUL bytes, no '/' characters, not "."
 * or ".."), checks for duplicates via dir_$find_entry, handles soft
 * link overflow page allocation if needed, then calls FUN_00e4f3ba
 * to actually insert the entry into the directory page.
 *
 * If the insertion fails and the directory's overflow flag (offset
 * 0x0E) is set, calls DIR_$CLEANUP to recover.
 *
 * Parameters:
 *   handle      - Directory handle
 *   name        - Entry name
 *   name_len    - Length of name
 *   entry_type  - Entry type (2=file, 3=hard link, 4=soft link)
 *   extra       - Extra data (e.g., node ID for type 3)
 *   uid         - UID of the entry's target object
 *   link_len    - Link data length (type 4 only)
 *   link_data   - Link data pointer / callback (type 4 only)
 *   status_ret  - Output: status code
 *
 * Original address: 0x00E4FE0A
 * Original size: 232 bytes
 */

#include "dir/dir_internal.h"

/* dir_$insert_entry - Actually insert the entry into directory pages
 *
 * This is the low-level insertion function that manages page allocation,
 * entry table updates, and data copying. Called after validation.
 *
 * Takes a pointer to the caller's frame (A1=A6) to access the full
 * parameter set at their stack offsets.
 *
 * Original address: 0x00E4F3BA
 * Size: 2640 bytes
 *
 * TODO: Analyze and emit FUN_00e4f3ba as a separate function.
 */
extern void FUN_00e4f3ba(uint16_t slot_idx, uint32_t param2,
                         uint16_t name_len, status_$t *status_ret);

/* DIR_$CLEANUP - Directory cleanup/recovery
 * Original address: 0x00E53578
 */
extern void DIR_$CLEANUP(void);

#ifndef status_$naming_invalid_leaf
#define status_$naming_invalid_leaf 0x000E000B
#endif

void dir_$add_entry(uint32_t handle, void *name, uint16_t name_len,
                    uint16_t entry_type, uint32_t extra, uid_t *uid,
                    uint16_t link_len, void *link_data, status_$t *status_ret)
{
    char *name_chars = (char *)name;
    int16_t i;
    int16_t remaining;
    char found;
    uint8_t lookup_buf[28];
    uint8_t lookup_buf2[112];
    uint16_t slot_info[2];  /* slot_info[0] = slot index */
    int16_t overflow_page;  /* offset -0xAA: overflow page index or -1 */

    /* Validate name: reject NUL bytes and '/' characters */
    remaining = name_len - 1;
    if (remaining >= 0) {
        i = 1;
        do {
            if (name_chars[i - 1] == '\0' || name_chars[i - 1] == '/') {
                goto invalid_leaf;
            }
            i++;
            remaining--;
        } while (remaining != -1);
    }

    /* Reject "." and ".." */
    if (*name_chars == '.') {
        if (name_len == 1) {
            goto invalid_leaf;
        }
        if (name_len == 2 && name_chars[1] == '.') {
invalid_leaf:
            *status_ret = status_$naming_invalid_leaf;
            return;
        }
    }

    /* Check for duplicate name */
    found = dir_$find_entry((void *)(uintptr_t)handle, name, name_len, 8,
                            (void **)lookup_buf, lookup_buf2, slot_info);
    if (found < 0) {
        /* Name already exists */
        *status_ret = status_$name_already_exists;
        return;
    }

    /* For soft links: check if total size exceeds page capacity */
    if (entry_type == 4 && ((int32_t)name_len + (int32_t)link_len) > 0x1B1) {
        /* Need overflow page for link data */
        dir_$alloc_overflow_page(status_ret);
        if (*status_ret != status_$ok) {
            return;
        }
    } else {
        overflow_page = -1;
    }

    /* Insert the entry into the directory pages.
     *
     * In the original m68k code, FUN_00e4f3ba receives a pointer
     * to the caller's frame (A1=A6) to access the full parameter
     * set (entry_type, extra, uid, link_len, link_data) at their
     * stack offsets. The slot_idx from dir_$find_entry is passed
     * explicitly along with name_len and status_ret.
     *
     * TODO: Once FUN_00e4f3ba is fully analyzed, convert this call
     * to pass all parameters explicitly.
     */
    FUN_00e4f3ba(slot_info[0], 0, name_len, status_ret);

    /* If insertion failed and overflow flag is set, try cleanup */
    if (*status_ret != status_$ok) {
        if (*(int8_t *)((char *)(uintptr_t)handle + 0x0E) < 0) {
            DIR_$CLEANUP();
        }
    }
}

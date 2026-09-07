/*
 * dir_$add_entry - Core internal add entry to directory
 *
 * Validates the entry name (no NUL bytes, no '/' characters, not "."
 * or ".."), checks for duplicates via dir_$find_entry, handles soft
 * link overflow page allocation if needed, then calls dir_$insert_entry
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
    uint16_t slot_info[2];  /* slot_info[0] = slot index / depth */

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
    found = dir_$find_entry(NAME_$HANDLE_TO_PTR(handle), name, name_len, 8,
                            (void **)lookup_buf, lookup_buf2, slot_info);
    if (found < 0) {
        /* Name already exists */
        *status_ret = status_$name_already_exists;
        return;
    }

    /* For soft links: check if total size exceeds page capacity */
    dir_insert_ctx_t ctx;

    if (entry_type == 4 && ((int32_t)name_len + (int32_t)link_len) > 0x1B1) {
        /* Need overflow page for link data */
        dir_$alloc_overflow_page(status_ret);
        if (*status_ret != status_$ok) {
            return;
        }
        /* TODO(source-qgq): overflow_page is set by alloc_overflow_page in the
         * original code via a parent frame variable. Need to verify
         * how the overflow page index is communicated. */
    } else {
        ctx.overflow_page = -1;
    }

    /* Populate the insertion context from our parameters and find_entry output.
     *
     * In the original M68K code, dir_$insert_entry was a nested Pascal
     * subprocedure that accessed all of this data directly via the parent
     * frame pointer (A1 = A6). Here we explicitly populate the context struct.
     */
    ctx.handle = handle;
    ctx.name = name;
    ctx.name_len = name_len;
    ctx.entry_type = entry_type;
    ctx.extra_val = extra;
    ctx.uid = uid;
    ctx.link_len = link_len;
    ctx.link_data = link_data;
    ctx.max_depth = slot_info[0];
    ctx.page_count = 0;

    /* Extract B-tree path from find_entry output.
     *
     * In the original M68K frame layout:
     *   Level 0 path entry: at A6-0x74 (= lookup_buf bytes 24-27)
     *   Level N (N>=1) path entry: at lookup_buf2[(N-1)*4]
     * Each level has 4 bytes: page_num (int16_t) + entry_idx (int16_t).
     *
     * The directory UID is stored in lookup_buf2 at offset 0x20 (high)
     * and 0x24 (low). Split page numbers start at offset 0x26.
     */
    ctx.path_page[0] = *(int16_t *)(lookup_buf + 24);
    ctx.path_entry[0] = *(int16_t *)(lookup_buf + 26);
    for (int16_t lvl = 1; lvl <= ctx.max_depth && lvl < DIR_MAX_BTREE_DEPTH; lvl++) {
        ctx.path_page[lvl] = *(int16_t *)(lookup_buf2 + (lvl - 1) * 4);
        ctx.path_entry[lvl] = *(int16_t *)(lookup_buf2 + (lvl - 1) * 4 + 2);
    }

    /* Extract directory UID from find_entry traversal state */
    ctx.dir_uid_high = *(uint32_t *)(lookup_buf2 + 0x20);
    ctx.dir_uid_low = *(uint32_t *)(lookup_buf2 + 0x24);

    /* Insert the entry into the directory pages */
    dir_$insert_entry(&ctx, slot_info[0], 0, name_len, status_ret);

    /* If insertion failed and overflow flag is set, try cleanup */
    if (*status_ret != status_$ok) {
        if (*(int8_t *)((char *)NAME_$HANDLE_TO_PTR(handle) + 0x0E) < 0) {
            DIR_$CLEANUP();
        }
    }
}

/*
 * dir_$remove_entry - Remove an entry from a directory by name
 *
 * Looks up a named entry, validates its type against the requested
 * operation type, extracts the UID (for types 2/3), removes the entry
 * from the page, and optionally truncates the directory if trailing
 * pages become empty.
 *
 * Type validation:
 *   op_type == 0: accept any type
 *   op_type == 4: require type 4 (link), else naming_not_a_link
 *   op_type != 4: if entry is type 4, set naming_invalid_link_operation
 *                 (but still allow removal with status set)
 *
 * After removing the entry from the page, if the entry was a link (type 4),
 * extracts the link page index and calls AST_$PURIFY and AST_$INVALIDATE
 * to clean up the overflow page.
 *
 * If the removal results in an empty trailing page (via the truncation
 * logic in unreachable blocks in Ghidra), the directory is truncated.
 *
 * Parameters:
 *   handle     - Directory handle (pointer to handle structure)
 *   name       - Entry name to remove
 *   name_len   - Length of name
 *   op_type    - Operation type for validation (0, 2, 4, etc.)
 *   uid_ret    - Output: UID of removed entry (for types 2/3)
 *   status_ret - Output: status code
 *
 * Original address: 0x00E50FC8
 * Original size: 530 bytes
 *
 * TODO(source-qgq): Ghidra marks several code blocks as unreachable (the directory
 * truncation logic at 0x00E51112..0x00E511A4). These blocks handle
 * truncating trailing empty pages after entry removal. They are likely
 * reached via a conditional that Ghidra's decompiler couldn't resolve.
 * The assembly shows this code IS reachable via the bpl at 0x00E5110E.
 */

#include "dir/dir_internal.h"

/* dir_$remove_entry_from_page - Remove entry from its directory page
 *
 * Low-level page manipulation to remove an entry. Takes a pointer to
 * the caller's frame (A1) to access lookup results stored at known
 * offsets. Handles both leaf pages and internal pages, compacts the
 * entry table, and adjusts the free space pointer.
 *
 * Original address: 0x00E50D5E
 */

void dir_$remove_entry(void *handle, void *name, int16_t name_len,
                       int16_t op_type, void *uid_ret, status_$t *status_ret)
{
    uint32_t *uid_out = (uint32_t *)uid_ret;
    char found;
    uint8_t *entry_ptr;
    void *entry_ret;
    uint16_t extra1[2];
    uint8_t extra2[32];
    int16_t slot_idx;
    uint16_t link_page;
    int8_t did_truncate;

    /* Look up the entry by name (flags=8 for add/modify context) */
    found = dir_$find_entry(handle, name, name_len, 8,
                            &entry_ret, extra2, (uint16_t *)&slot_idx);
    entry_ptr = (uint8_t *)entry_ret;

    if (found >= 0) {
        /* Entry not found */
        *status_ret = status_$naming_name_not_found;
        return;
    }

    /* For types 2 and 3, copy the UID to the output */
    if ((*entry_ptr & 7) == 2 || (*entry_ptr & 7) == 3) {
        uid_out[0] = *(uint32_t *)(entry_ptr + 4);
        uid_out[1] = *(uint32_t *)(entry_ptr + 8);
    }

    *status_ret = status_$ok;

    /* Validate entry type against requested operation type */
    if (op_type != 0) {
        if (op_type == 4) {
            /* Want to remove a link - verify it IS a link */
            if ((*entry_ptr & 7) != 4) {
                *status_ret = status_$naming_not_a_link;
                goto check_proceed;
            }
        } else {
            /* Non-link operation - reject link entries */
            if ((*entry_ptr & 7) == 4) {
                *status_ret = status_$naming_invalid_link_operation;
            }
        }
    }

check_proceed:
    {
        int proceed = (*status_ret == status_$ok);
        if (!proceed) {
            return;
        }
    }

    /* Extract link page index for type 4 entries */
    if ((*entry_ptr & 7) == 4) {
        link_page = *(uint16_t *)(entry_ptr + 4);
    } else {
        link_page = 0xFFFF;
    }

    /* Remove the entry from the page */
    did_truncate = 0;
    dir_$remove_entry_from_page(slot_idx, status_ret);

    /* Clean up link overflow page if applicable */
    if (link_page != 0xFFFF && *status_ret == status_$ok) {
        uid_t *handle_uid = (uid_t *)handle;

        if ((int8_t)did_truncate >= 0) {
            /* AST_$PURIFY the overflow page reference */
            uint32_t purify_data;
            purify_data = (uint32_t)*(uint16_t *)(extra2 + slot_idx * 4 - 4);
            AST_$PURIFY(handle_uid, 0x12, 0, &purify_data, 1, status_ret);
        }

        if (*status_ret == status_$ok) {
            /* Invalidate the overflow page */
            uid_t inv_uid;
            /* A6-0x44: AST_$INVALIDATE's status longword
             * (`clr.l (A3)` at 0x00E0664C); the caller never inspects it. */
            status_$t inv_status;
            inv_uid.high = handle_uid->high;
            inv_uid.low = handle_uid->low;
            AST_$INVALIDATE(&inv_uid, (uint32_t)link_page, 1, 0xFFFF,
                            &inv_status);
        }
    }

    /* TODO(source-qgq): The assembly contains additional truncation logic at
     * 0x00E51112-0x00E511A4 that handles shrinking the directory
     * when trailing pages become empty. Ghidra marks these as
     * unreachable blocks, but they are reached via the did_truncate
     * flag (offset -0x60 in the frame). This truncation walks
     * backward from the last page, finds the last non-empty page,
     * calculates the new size, calls AST_$TRUNCATE, and updates
     * the directory's total size and multi-page flag in page 0.
     */
}

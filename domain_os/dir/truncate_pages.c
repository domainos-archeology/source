/*
 * dir_$truncate_pages - Truncate/resize directory pages
 *
 * Truncates a directory to the given number of pages by calling
 * AST_$INVALIDATE on the excess.  Clears dir_$handle_t.split_busy (the
 * `clr.b (0xe,A2)` at 0x00E4E94C, the flag dir_$alloc_split_page sets) and
 * zeros the status.
 *
 * The number of pages to invalidate is
 *   (dir_$handle_t.length >> 10) - new_page_count
 * (`lsr.l #0x8` then `lsr.l #0x2` on the longword at handle+0x10).
 *
 * Parameters:
 *   handle         - Directory handle (pointer to handle structure)
 *   new_page_count - Number of pages to keep
 *   status_ret     - Output: status code (always set to 0)
 *
 * Returns: 0 (always)
 *
 * Original address: 0x00E4E90A
 * Original size: 86 bytes
 */

#include "dir/dir_internal.h"

/* AST_$INVALIDATE - declared in ast/ast.h (included via dir_internal.h)
 * Canonical signature: AST_$INVALIDATE(uid_t *uid, uint32_t start_page,
 *                      uint32_t count, int16_t flags, status_$t *status) */

uint32_t dir_$truncate_pages(void *handle, uint16_t new_page_count,
                             status_$t *status_ret)
{
    dir_$handle_t *h = (dir_$handle_t *)handle;     /* A2 */
    uid_t local_uid;            /* A6-0x10 */
    status_$t local_status;     /* A6-0x14 */
    uint32_t total_pages;       /* D3 */
    int32_t pages_to_remove;

    /* 0x00E4E92A-0x00E4E92E: the UID is copied into a frame cell first. */
    local_uid.high = h->uid.high;
    local_uid.low  = h->uid.low;

    /* 0x00E4E91C-0x00E4E926 */
    total_pages = h->length >> 10;

    /* Compute pages to remove */
    pages_to_remove = (int16_t)(total_pages - new_page_count);

    /* Invalidate the excess pages */
    AST_$INVALIDATE(&local_uid, (uint32_t)new_page_count,
                    (uint32_t)pages_to_remove, (int16_t)-1, &local_status);

    /* 0x00E4E94C `clr.b (0xe,A2)` */
    h->split_busy = 0;

    /* Clear status */
    *status_ret = status_$ok;

    return 0;
}

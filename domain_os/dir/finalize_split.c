/*
 * dir_$finalize_split - Finalize page split, update parent arrays and truncate
 *
 * Called after dir_$insert_entry has completed the B-tree split operations.
 * This function:
 *   1. Saves the last base page number (used later as the new page count
 *      for truncation).
 *   2. Copies the updated path_page[] values into split_pages[1..] so
 *      that purify can flush the correct set of pages.
 *   3. Calls dir_$purify_split_pages to sort and flush.
 *   4. On success, calls dir_$truncate_pages to remove excess pages that
 *      were temporarily allocated during the split but are no longer needed.
 *
 * Originally a nested Pascal subprocedure of dir_$insert_entry. Accesses
 * the shared insertion context via the parent frame pointer chain
 * (A6 -> caller's A6 -> *(caller_A6-4) = ctx). In the C flattening,
 * ctx is passed explicitly.
 *
 * Parameters:
 *   ctx        - Shared insertion context (add_entry's frame data)
 *   status_ret - Output: status code
 *
 * Original address: 0x00E4EECA
 * Original size: 120 bytes
 */

#include "dir/dir_internal.h"

void dir_$finalize_split(dir_insert_ctx_t *ctx, status_$t *status_ret)
{
    int16_t diff = ctx->max_depth - ctx->current_slot;

    /* Save split_pages[1 + diff] before the loop overwrites it.
     * This is the last allocated base page number from alloc_split_page,
     * used as the new_page_count for dir_$truncate_pages.
     * Original: D0w = (max_depth - current_slot) * 2, then
     *   move.w (-0x48,A2,D0w*0x1),(-0x12,A6) */
    uint16_t new_page_count = (uint16_t)ctx->split_pages[1 + diff];

    /* Copy path page numbers into split_pages[1..1+diff].
     * After the split, path_page[] reflects the final B-tree structure.
     * The split_pages array is updated to contain these page numbers so
     * that dir_$purify_split_pages can flush the correct pages.
     * Original: loop at 0x00E4EEF8-0x00E4EF12, runs diff+1 times via dbf */
    if (diff >= 0) {
        int16_t i = 0;
        int16_t src_level = ctx->current_slot;
        int16_t count = diff;
        do {
            ctx->split_pages[1 + i] = ctx->path_page[src_level];
            src_level++;
            i++;
            count--;
        } while (count != -1);
    }

    /* Sort and purify the split pages.
     * Original: bsr dir_$purify_split_pages at 0x00E4EF1C */
    dir_$purify_split_pages(ctx, status_ret);

    if (*status_ret == status_$ok) {
        /* Truncate the directory to remove excess pages.
         * Original: bsr dir_$truncate_pages at 0x00E4EF34
         * with handle from ctx->handle (A2+0x08) and saved page count */
        dir_$truncate_pages((void *)(uintptr_t)ctx->handle,
                            new_page_count, status_ret);
    }
}

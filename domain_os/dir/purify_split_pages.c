/*
 * dir_$purify_split_pages - Sort and purify allocated split pages
 *
 * Sorts split_pages[1..page_count] in descending order (by unsigned
 * comparison), copies the sorted page numbers into a zero-extended
 * uint32_t array, then calls AST_$PURIFY to flush the pages.
 *
 * Originally a nested Pascal subprocedure accessed via parent frame
 * pointer (A1 = insert_entry's A6, which chained to add_entry's
 * frame at A2 = ctx). In the C flattening, ctx is passed directly.
 *
 * Called by both dir_$alloc_split_page and dir_$finalize_split.
 *
 * Parameters:
 *   ctx        - Shared insertion context containing split_pages and page_count
 *   status_ret - Output: status code from AST_$PURIFY
 *
 * Original address: 0x00E4EA9C
 * Original size: 164 bytes
 */

#include "dir/dir_internal.h"

void dir_$purify_split_pages(dir_insert_ctx_t *ctx, status_$t *status_ret)
{
    int16_t page_count = ctx->page_count;

    /* Bubble/selection sort split_pages[1..page_count] in descending order.
     * Original M68K: outer from index 1 to page_count-1, inner from
     * outer+1 to page_count, swap if inner > outer (unsigned). */
    {
        int16_t outer_count = page_count - 2;
        if (outer_count >= 0) {
            int16_t i = 1;
            do {
                int16_t j = i + 1;
                int16_t inner_count = page_count - j;
                if (inner_count >= 0) {
                    do {
                        if ((uint16_t)ctx->split_pages[j] >
                            (uint16_t)ctx->split_pages[i]) {
                            int16_t temp = ctx->split_pages[i];
                            ctx->split_pages[i] = ctx->split_pages[j];
                            ctx->split_pages[j] = temp;
                        }
                        j++;
                        inner_count--;
                    } while (inner_count != -1);
                }
                i = j;  /* Original: move.w D2w,D1w after inner loop */
                outer_count--;
            } while (outer_count != -1);
        }
    }

    /* Copy sorted page numbers to uint32_t array (zero-extended).
     * Original used a local array on the stack at A6-0x50. */
    uint32_t page_array[16];
    {
        int16_t count = page_count - 1;
        if (count >= 0) {
            int16_t idx = 1;
            int16_t arr_idx = 0;
            do {
                page_array[arr_idx] = (uint32_t)(uint16_t)ctx->split_pages[idx];
                arr_idx++;
                idx++;
                count--;
            } while (count != -1);
        }
    }

    /* Call AST_$PURIFY to flush the allocated pages.
     * param_2=0x12: purify page operation
     * param_3=0: no offset
     * param_4=page_array: array of page numbers
     * param_5=page_count: number of pages */
    AST_$PURIFY((uid_t *)NAME_$HANDLE_TO_PTR(ctx->handle),
                0x12, 0, page_array, page_count, status_ret);
}

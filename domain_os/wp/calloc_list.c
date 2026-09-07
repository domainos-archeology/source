/*
 * wp/calloc_list.c - WP_$CALLOC_LIST implementation
 *
 * Allocates multiple physical pages. Acquires the WP lock, calls
 * ast_$allocate_pages with `count` as BOTH of its leading word
 * parameters, then releases the lock.
 *
 * Original address: 0x00e07138
 */

#include "wp/wp_internal.h"
#include "ast/ast.h"

/*
 * WP_$CALLOC_LIST - Allocate multiple wired physical pages
 *
 * Parameters:
 *   count   - Number of pages to allocate
 *   ppn_arr - Array to receive physical page numbers
 *
 * The assembly pushes `count` twice:
 *   move.w (0x8,A6),-(SP)     ; the minimum-count word at (0xa,A6)
 *   move.w (SP),-(SP)         ; the requested-count word at (0x8,A6)
 *
 * i.e. request `count` pages, with the minimum required also `count`.
 */
void WP_$CALLOC_LIST(int16_t count, uint32_t *ppn_arr)
{
    /*
     * Two word arguments, both `count`: ast_$allocate_pages takes the
     * requested count at (0x8,A6) and the minimum at (0xa,A6).
     */
    ML_$LOCK(WP_LOCK_ID);
    ast_$allocate_pages(count, count, ppn_arr);
    ML_$UNLOCK(WP_LOCK_ID);
}

/*
 * wp/calloc_list.c - WP_$CALLOC_LIST implementation
 *
 * Allocates multiple physical pages. Acquires the WP lock, calls
 * ast_$allocate_pages with count duplicated into both halves of the
 * count_flags parameter, then releases the lock.
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
 * The assembly duplicates `count` into both halves of the 32-bit
 * count_flags parameter passed to ast_$allocate_pages:
 *   move.w (0x8,A6),-(SP)     ; push count (low word)
 *   move.w (SP),-(SP)         ; duplicate it (high word)
 *
 * This means: request `count` pages, with minimum required also `count`.
 */
void WP_$CALLOC_LIST(int16_t count, uint32_t *ppn_arr)
{
    uint32_t count_flags;

    /* Pack count into both halves: high word = min required, low word = requested */
    count_flags = ((uint32_t)(uint16_t)count << 16) | (uint16_t)count;

    ML_$LOCK(WP_LOCK_ID);
    ast_$allocate_pages(count_flags, ppn_arr);
    ML_$UNLOCK(WP_LOCK_ID);
}

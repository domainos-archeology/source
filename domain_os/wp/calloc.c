/*
 * wp/calloc.c - WP_$CALLOC implementation
 *
 * Allocates a single physical page. Acquires the WP lock, calls
 * ast_$allocate_pages to allocate one page into a local buffer,
 * releases the lock, then copies the result out.
 *
 * ast_$allocate_pages takes THREE Pascal parameters -- a requested count
 * word at (0x8,A6), a minimum-count word at (0xa,A6) and the array
 * pointer at (0xc,A6) -- and this caller pushes 1, 1 and the buffer.
 *
 * Original address: 0x00e070ec
 */

#include "wp/wp_internal.h"
#include "ast/ast.h"

/*
 * WP_$CALLOC - Allocate a single wired physical page
 *
 * Parameters:
 *   ppn_out - Pointer to receive physical page number
 *   status  - Receives status code (always status_$ok)
 *
 * The 128-byte local buffer (32 uint32_t) matches the stack frame
 * observed in the assembly (link.w A6,-0x80).
 *
 * The two words are both 1: request one page, and one page is also the
 * minimum that must be obtained.
 */
void WP_$CALLOC(uint32_t *ppn_out, status_$t *status)
{
    uint32_t ppn_buf[32];  /* 128 bytes = 0x80, matches stack frame */

    ML_$LOCK(WP_LOCK_ID);
    ast_$allocate_pages(1, 1, ppn_buf);   /* count = 1, min_count = 1 */
    ML_$UNLOCK(WP_LOCK_ID);

    *ppn_out = ppn_buf[0];
    *status = status_$ok;
}

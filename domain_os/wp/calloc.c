/*
 * wp/calloc.c - WP_$CALLOC implementation
 *
 * Allocates a single physical page. Acquires the WP lock, calls
 * ast_$allocate_pages to allocate one page into a local buffer,
 * releases the lock, then copies the result out.
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
 * count_flags = 0x00010001:
 *   low word  = 1 (request 1 page)
 *   high word = 1 (minimum 1 page required)
 */
void WP_$CALLOC(uint32_t *ppn_out, status_$t *status)
{
    uint32_t ppn_buf[32];  /* 128 bytes = 0x80, matches stack frame */

    ML_$LOCK(WP_LOCK_ID);
    ast_$allocate_pages(0x00010001, ppn_buf);
    ML_$UNLOCK(WP_LOCK_ID);

    *ppn_out = ppn_buf[0];
    *status = status_$ok;
}

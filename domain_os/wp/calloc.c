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
 *
 * 0x00E070EC - 0x00E07136 (76 bytes).  Verified against the disassembly
 * 2026-09-27; faithful.  Saves A5 and loads 0xE1DC80 (the AST data block,
 * unused by the body); ML_$LOCK(0x14); ast_$allocate_pages(1, 1, buf) with
 * the two words pushed as one `move.l #0x10001' and the 0x80-byte frame
 * buffer at (-0x80,A6); ML_$UNLOCK(0x14); *ppn_out = buf[0]; *status = 0.
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

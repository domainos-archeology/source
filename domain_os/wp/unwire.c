/*
 * wp/unwire.c - WP_$UNWIRE implementation
 *
 * Unwire previously wired memory. Acquires the WP lock, unwires
 * the page, then releases the lock.
 *
 * Original address: 0x00e07176
 *
 * 0x00E07176 - 0x00E071AE (58 bytes).  Verified against the disassembly
 * 2026-09-27; faithful: MMAP_$UNWIRE((0x8,A6)) under ML lock 0x14, A5
 * saved/loaded with 0xE1DC80 and unused.
 */

#include "wp/wp_internal.h"
#include "mmap/mmap.h"

/*
 * WP_$UNWIRE - Unwire previously wired memory
 *
 * Parameters:
 *   wired_addr - Address returned by WP_$CALLOC
 */
void WP_$UNWIRE(uint32_t wired_addr)
{
    ML_$LOCK(WP_LOCK_ID);
    MMAP_$UNWIRE(wired_addr);
    ML_$UNLOCK(WP_LOCK_ID);
}

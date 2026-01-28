/*
 * wp/unwire.c - WP_$UNWIRE implementation
 *
 * Unwire previously wired memory. Acquires the WP lock, unwires
 * the page, then releases the lock.
 *
 * Original address: 0x00e07176
 */

#include "wp/wp.h"
#include "ml/ml.h"
#include "mmap/mmap.h"

/* WP subsystem lock ID (same as ML_LOCK_PMAP) */
#define WP_LOCK_ID 0x14

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

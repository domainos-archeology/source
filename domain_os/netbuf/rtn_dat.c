/*
 * NETBUF_$RTN_DAT - Return a data buffer
 *
 * Returns a data buffer to the pool if under limit, otherwise frees it.
 *
 * Original address: 0x00E0F046
 *
 * If the pool is at capacity (dat_cnt >= dat_lim), the page is freed
 * via MMAP_$FREE instead of being returned to the pool.
 */

#include "netbuf/netbuf_internal.h"

void NETBUF_$RTN_DAT(uint32_t addr)
{
    ml_$spin_token_t token;
    uint32_t ppn;

    ppn = addr >> 10;

    token = ML_$SPIN_LOCK(&NETBUF_$SPIN_LOCK);

    if (NETBUF_$DAT_CNT < NETBUF_$DAT_LIM) {
        /* Add to free list */
        NETBUF_DAT_NEXT(ppn) = (uint16_t)NETBUF_$DAT_TOP;
        NETBUF_$DAT_TOP = ppn;
        NETBUF_$DAT_CNT++;
        ML_$SPIN_UNLOCK(&NETBUF_$SPIN_LOCK, token);
    } else {
        /* Pool is full, free the page */
        ML_$SPIN_UNLOCK(&NETBUF_$SPIN_LOCK, token);
        MMAP_$FREE(ppn);
    }
}

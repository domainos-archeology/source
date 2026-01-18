/*
 * NETBUF_$DEL_PAGES - Delete pages from buffer pools
 *
 * Removes pages from header and data buffer pools and frees them.
 *
 * Original address: 0x00E0EB26
 *
 * Assembly analysis:
 *   - Calculates how many header buffers can actually be freed
 *   - Maintains minimum of 39 (0x27) header buffers
 *   - Frees header buffers via netbuf_rtnva_locked + MMAP_$FREE
 *   - Maintains minimum of 10 data buffers
 *   - Frees data buffers via MMAP_$FREE
 */

#include "netbuf/netbuf_internal.h"

void NETBUF_$DEL_PAGES(int16_t hdr_count, int16_t dat_count)
{
    ml_$spin_token_t token;
    int16_t hdr_to_free;
    int16_t dat_to_free;
    int16_t i;
    uint32_t va;
    uint32_t ppn;

    /* Acquire spin lock */
    token = ML_$SPIN_LOCK(&NETBUF_$SPIN_LOCK);

    /* Calculate how many header buffers we can actually free
     * If HDR_ALLOC >= 0xB0, we can free up to (HDR_ALLOC - 0xB0) + hdr_count
     * Otherwise, we can free up to min(hdr_count, HDR_ALLOC - 0x27)
     */
    if (NETBUF_$HDR_ALLOC >= NETBUF_HDR_MAX) {
        hdr_to_free = hdr_count - (NETBUF_$HDR_ALLOC - NETBUF_HDR_MAX);
        if (hdr_to_free < 0) {
            hdr_to_free = 0;
        }
    } else {
        hdr_to_free = hdr_count;
        if (hdr_to_free > NETBUF_$HDR_ALLOC - 39) {
            hdr_to_free = NETBUF_$HDR_ALLOC - 39;
        }
    }

    if (hdr_to_free < 0) {
        CRASH_SYSTEM(&netbuf_err);
    }

    /* Update header allocation count */
    NETBUF_$HDR_ALLOC -= hdr_count;

    ML_$SPIN_UNLOCK(&NETBUF_$SPIN_LOCK, token);

    /* Free header buffers */
    token = ML_$SPIN_LOCK(&NETBUF_$SPIN_LOCK);

    for (i = 0; i < hdr_to_free; i++) {
        /* Remove from header free list */
        va = NETBUF_$HDR_TOP;
        NETBUF_$HDR_TOP = NETBUF_HDR_NEXT(va);

        /* Return VA slot and get physical address */
        ppn = netbuf_rtnva_locked(&va);

        /* Free the physical page */
        MMAP_$FREE(ppn >> 10);
    }

    ML_$SPIN_UNLOCK(&NETBUF_$SPIN_LOCK, token);

    /* Free data buffers */
    token = ML_$SPIN_LOCK(&NETBUF_$SPIN_LOCK);

    /* Calculate how many data buffers we can free
     * Maintain minimum of 10 data buffers
     */
    dat_to_free = dat_count;
    if (dat_to_free > (int32_t)NETBUF_$DAT_CNT - 10) {
        dat_to_free = (int16_t)(NETBUF_$DAT_CNT - 10);
    }

    for (i = 0; i < dat_to_free; i++) {
        ppn = NETBUF_$DAT_TOP;
        if (ppn == 0) {
            break;
        }

        /* Remove from data free list */
        NETBUF_$DAT_TOP = NETBUF_DAT_NEXT(ppn);

        /* Free the physical page */
        MMAP_$FREE(ppn);

        NETBUF_$DAT_CNT--;
    }

    ML_$SPIN_UNLOCK(&NETBUF_$SPIN_LOCK, token);
}

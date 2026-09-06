/*
 * PMAP_$PURGE_WS - Purge a working set
 *
 * Purges pages from a working set. If flags is negative, purges
 * using the high mark index. Otherwise frees the working set list.
 *
 * Parameters:
 *   index - Working set index (0-63)
 *   flags - If negative, purge using high mark; otherwise free WSL
 *
 * Original address: 0x00e146b4
 */

#include "pmap/pmap_internal.h"

/* MMAP_$WSL_HI_MARK (0xE23CA6) is indexed here by process id; the same
 * array is exported by mmap/mmap.h as MMAP_PID_TO_WSL. */

void PMAP_$PURGE_WS(int16_t index, int16_t flags)
{
    ML_$LOCK(PMAP_LOCK_ID);

    if (flags < 0) {
        /* Purge using the working set list high mark */
        uint16_t slot = MMAP_PID_TO_WSL[index];
        MMAP_$PURGE(slot);
    } else {
        /* Free the working set list entry */
        MMAP_$FREE_WSL(index);
    }

    ML_$UNLOCK(PMAP_LOCK_ID);
}

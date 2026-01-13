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

#include "pmap.h"

void PMAP_$PURGE_WS(int16_t index, int16_t flags)
{
    ML_$LOCK(PMAP_LOCK_ID);

    if (flags < 0) {
        /* Purge using the working set list high mark */
        uint16_t slot = MMAP_$WSL_HI_MARK[index];
        MMAP_$PURGE(slot);
    } else {
        /* Free the working set list entry */
        MMAP_$FREE_WSL(index);
    }

    ML_$UNLOCK(PMAP_LOCK_ID);
}

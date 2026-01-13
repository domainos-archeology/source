/*
 * MMAP_$ALLOC_FREE - Allocate free pages
 *
 * Allocates pages from the global free pool (WSL index 0).
 * Uses spin lock for synchronization.
 *
 * Original address: 0x00e0d870
 */

#include "mmap.h"

uint16_t MMAP_$ALLOC_FREE(uint32_t *vpn_array, uint16_t count)
{
    uint16_t token = ML_$SPIN_LOCK(MMAP_GLOBALS);

    ws_hdr_t *free_pool = WSL_FOR_INDEX(WSL_INDEX_FREE_POOL);

    if (free_pool->page_count == 0) {
        ML_$SPIN_UNLOCK(MMAP_GLOBALS, token);
        return 0;
    }

    /* Limit allocation to available pages */
    uint16_t to_alloc = (free_pool->page_count < count) ?
                        (uint16_t)free_pool->page_count : count;

    mmap_$alloc_pages_from_wsl(free_pool, vpn_array, to_alloc);

    ML_$SPIN_UNLOCK(MMAP_GLOBALS, token);

    MMAP_$ALLOC_CNT++;
    MMAP_$ALLOC_PAGES += to_alloc;

    return to_alloc;
}

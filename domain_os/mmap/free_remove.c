/*
 * MMAP_$FREE_REMOVE - Remove a page from its WSL and free it
 *
 * First removes the page from any linked list it may be in,
 * then frees it to the free pool.
 *
 * Original address: 0x00e0cb86
 */

#include "mmap.h"

void MMAP_$FREE_REMOVE(mmape_t *page, uint32_t vpn)
{
    uint16_t token;

    /* If page is in a WSL, remove it first */
    if (page->flags1 & MMAPE_FLAG1_IN_WSL) {
        mmap_$remove_from_wsl(page, vpn);
    }

    token = ML_$SPIN_LOCK(MMAP_GLOBALS);

    /* Clear "on disk" flag */
    page->flags2 &= ~MMAPE_FLAG2_ON_DISK;

    /* Add to WSL 0 (free pool), inserting at tail */
    mmap_$add_to_wsl(page, vpn, 0, -1);

    ML_$SPIN_UNLOCK(MMAP_GLOBALS, token);
}

/*
 * MMAP_$FREE - Free a single page
 *
 * Releases a page by clearing its "on disk" flag and adding it
 * to the appropriate free list. Uses spin lock for synchronization.
 *
 * Original address: 0x00e0cac2
 */

#include "mmap.h"

void MMAP_$FREE(uint32_t vpn)
{
    uint16_t token;

    token = ML_$SPIN_LOCK(MMAP_GLOBALS);

    mmape_t *page = MMAPE_FOR_VPN(vpn);

    /* Clear "on disk" flag */
    page->flags2 &= ~MMAPE_FLAG2_ON_DISK;

    /* Add to WSL 0 (free pool), inserting at tail */
    mmap_$add_to_wsl(page, vpn, 0, -1);

    ML_$SPIN_UNLOCK(MMAP_GLOBALS, token);
}

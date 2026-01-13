/*
 * MMAP_$FREE_LIST - Free a list of pages
 *
 * Walks a linked list of pages (via next_vpn) and frees each one.
 * Uses spin lock for synchronization.
 *
 * Original address: 0x00e0cb20
 */

#include "mmap.h"

void MMAP_$FREE_LIST(uint32_t vpn_head)
{
    uint16_t token;

    token = ML_$SPIN_LOCK(MMAP_GLOBALS);

    while (vpn_head != 0) {
        mmape_t *page = MMAPE_FOR_VPN(vpn_head);
        uint16_t next = page->next_vpn;

        /* Add to WSL 0 (free pool), inserting at tail */
        mmap_$add_to_wsl(page, vpn_head, 0, -1);

        vpn_head = next;
    }

    ML_$SPIN_UNLOCK(MMAP_GLOBALS, token);
}

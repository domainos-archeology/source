/*
 * mmap_$alloc_pages_from_wsl - Allocate pages from a specific WSL
 *
 * Removes pages from the head of a WSL and returns them in an array.
 * Updates the doubly-linked list to maintain consistency.
 *
 * Original address: 0x00e0d6f8
 */

#include "mmap.h"

void mmap_$alloc_pages_from_wsl(ws_hdr_t *wsl, uint32_t *vpn_array, uint16_t count)
{
    uint32_t first_vpn = wsl->head_vpn;
    uint32_t current_vpn = first_vpn;

    /* Collect pages into the output array */
    for (uint16_t i = 0; i < count; i++) {
        vpn_array[i] = current_vpn;
        mmape_t *page = MMAPE_FOR_VPN(current_vpn);
        page->flags1 &= ~MMAPE_FLAG1_IN_WSL;
        current_vpn = page->prev_vpn;
    }

    /* Update WSL page count */
    wsl->page_count -= count;

    if (wsl->page_count != 0) {
        /* Relink the list around the removed pages */
        mmape_t *first = MMAPE_FOR_VPN(first_vpn);
        uint16_t tail = first->next_vpn;

        MMAPE_FOR_VPN(tail)->prev_vpn = (uint16_t)current_vpn;
        MMAPE_FOR_VPN(current_vpn)->next_vpn = tail;
        wsl->head_vpn = current_vpn;
    }

    MMAP_$PAGEABLE_PAGES_LOWER_LIMIT -= count;
}

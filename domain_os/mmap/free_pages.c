/*
 * MMAP_$FREE_PAGES - Free an array of pages
 *
 * Removes an array of pages from their respective WSLs and adds
 * them to the global free pool (WSL index 0). Uses spin lock for
 * synchronization.
 *
 * Original address: 0x00e0ce56
 */

#include "mmap.h"
#include "mmap_internal.h"
#include "misc/misc.h"

void MMAP_$FREE_PAGES(uint32_t *vpn_array, uint16_t count)
{
    if (count == 0) return;

    uint16_t token = ML_$SPIN_LOCK(MMAP_GLOBALS);

    uint32_t first_vpn = vpn_array[0];
    uint32_t last_vpn = vpn_array[count - 1];
    uint32_t prev_vpn = 0;

    /* Process each page in the array */
    for (uint16_t i = 0; i < count; i++) {
        uint32_t vpn = vpn_array[i];
        uint32_t next_vpn = (i < count - 1) ? vpn_array[i + 1] : 0;
        mmape_t *page = MMAPE_FOR_VPN(vpn);

        /* Validate page is in a WSL */
        if (!(page->flags1 & MMAPE_FLAG1_IN_WSL)) {
            CRASH_SYSTEM(Inconsistent_MMAPE_Err);
        }

        /* Remove from current WSL */
        uint16_t wsl_index = page->wsl_index;
        ws_hdr_t *wsl = WSL_FOR_INDEX(wsl_index);

        uint16_t prev = page->next_vpn;
        uint16_t next = page->prev_vpn;
        MMAPE_FOR_VPN(prev)->prev_vpn = next;
        MMAPE_FOR_VPN(next)->next_vpn = prev;

        if (vpn == wsl->head_vpn) {
            wsl->head_vpn = page->prev_vpn;
        }

        wsl->page_count--;

        /* Set up for linking into free pool */
        page->prev_vpn = (uint16_t)next_vpn;
        page->next_vpn = (uint16_t)prev_vpn;
        page->wsl_index = WSL_INDEX_FREE_POOL;
        page->priority &= 0x3F;  /* Clear high bits */

        prev_vpn = vpn;
    }

    /* Link the freed pages into the free pool (WSL 0) */
    ws_hdr_t *free_pool = WSL_FOR_INDEX(WSL_INDEX_FREE_POOL);

    if (free_pool->page_count == 0) {
        MMAPE_FOR_VPN(first_vpn)->next_vpn = (uint16_t)last_vpn;
        MMAPE_FOR_VPN(last_vpn)->prev_vpn = (uint16_t)first_vpn;
        free_pool->head_vpn = first_vpn;
    } else {
        uint32_t head_vpn = free_pool->head_vpn;
        mmape_t *head = MMAPE_FOR_VPN(head_vpn);
        uint16_t tail_vpn = head->next_vpn;

        head->next_vpn = (uint16_t)last_vpn;
        MMAPE_FOR_VPN(tail_vpn)->prev_vpn = (uint16_t)first_vpn;
        MMAPE_FOR_VPN(first_vpn)->next_vpn = tail_vpn;
        MMAPE_FOR_VPN(last_vpn)->prev_vpn = (uint16_t)head_vpn;
    }

    free_pool->page_count += count;

    ML_$SPIN_UNLOCK(MMAP_GLOBALS, token);
}

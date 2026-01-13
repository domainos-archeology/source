/*
 * MMAP_$GET_IMPURE - Get impure pages from a working set list
 *
 * Collects impure (dirty/modified) pages from a WSL into an array.
 * Used for writeout scheduling. Optionally limits to first 100 pages.
 *
 * Original address: 0x00e0d5ea
 */

#include "mmap.h"

/* Segment info table */
extern void *SEGMENT_TABLE[];  /* At 0xEC5400 */

void MMAP_$GET_IMPURE(uint16_t wsl_index, uint32_t *vpn_array, int8_t all_pages,
                       uint16_t max_pages, uint32_t *scanned, uint16_t *returned)
{
    ws_hdr_t *wsl = WSL_FOR_INDEX(wsl_index);

    uint32_t scan_count = 0;
    uint16_t return_count = 0;
    uint32_t max_scan;

    if (all_pages < 0) {
        max_scan = wsl->page_count;
    } else {
        max_scan = (wsl->page_count < 100) ? wsl->page_count : 100;
    }

    uint32_t current_vpn = wsl->head_vpn;

    while (return_count < max_pages && scan_count < max_scan) {
        mmape_t *page = MMAPE_FOR_VPN(current_vpn);
        uint16_t next_vpn = page->prev_vpn;

        boolean is_impure = false;

        /* Check if page should be included based on segment type */
        if (!(page->flags2 & MMAPE_FLAG2_ON_DISK)) {
            uint16_t seg = page->segment;
            void *seg_info = SEGMENT_TABLE[seg];
            if ((*(uint16_t*)((char*)seg_info + 0x0E)) & 0x1000) {
                is_impure = true;
            }
        }

        if (is_impure) {
            /* Add to output array */
            vpn_array[return_count++] = current_vpn;

            /* Unlink from WSL */
            uint16_t prev = page->next_vpn;
            uint16_t next = page->prev_vpn;
            MMAPE_FOR_VPN(prev)->prev_vpn = next;
            MMAPE_FOR_VPN(next)->next_vpn = prev;

            /* Clear flags */
            page->flags1 &= ~MMAPE_FLAG1_IN_WSL;
            page->flags2 &= ~MMAPE_FLAG2_MODIFIED;
        }

        current_vpn = next_vpn;
        scan_count++;
    }

    /* Update WSL page count */
    wsl->page_count -= return_count;
    if (wsl->page_count != 0) {
        wsl->head_vpn = current_vpn;
    }

    *returned = return_count;
    *scanned = scan_count;

    MMAP_$PAGEABLE_PAGES_LOWER_LIMIT -= return_count;
}

/*
 * MMAP_$GET_IMPURE - Get impure pages from a working set list
 *
 * Collects impure (dirty/modified) pages from a WSL into an array.
 * Used for writeout scheduling. Optionally limits to first 100 pages.
 *
 * Original address: 0x00e0d5ea
 */

#include "mmap/mmap_internal.h"

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

        /*
         * Check if page should be included based on the owning object's
         * attribute flags:
         *
         *   00e0d662  tst.b (-0x1ff7,A0) / bmi  ; skip when ON_DISK
         *   00e0d668  move.w (-0x1ffe,A0),D6w   ; mmape->segment
         *   00e0d66c  movea.l #0xec5400,A3
         *   00e0d672  lsl.w #0x2,D6w
         *   00e0d674  move.w D6w,D7w
         *   00e0d676  lsl.w #0x2,D7w
         *   00e0d678  add.w D7w,D6w             ; seg * 0x14
         *   00e0d67a  lea (0x0,A3,D6w),A3
         *   00e0d67e  movea.l (-0x10,A3),A4     ; SEG_ASTE(seg)->aote
         *   00e0d682  move.w (0xe,A4),D7w       ; attribute flags word
         *   00e0d686  btst.l #0xc,D7
         */
        if (!(page->flags2 & MMAPE_FLAG2_ON_DISK)) {
            aote_t *aote = MMAP_$SEG_ASTE_FOR(page->segment)->aote;
            if (MMAP_AOTE_ATTR_FLAGS(aote) & MMAP_AOTE_ATTR_FLAG_BIT12) {
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

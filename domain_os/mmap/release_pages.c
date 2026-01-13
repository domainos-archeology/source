/*
 * MMAP_$RELEASE_PAGES - Release pages for a process
 *
 * Processes an array of page numbers and releases those belonging
 * to the specified process. Each page is moved to an appropriate
 * free list based on its type (pure/impure/dirty).
 *
 * Original address: 0x00e0cff8
 */

#include "mmap.h"

/* Segment info table (for determining if segment needs flush) */
extern void *SEGMENT_TABLE[];  /* At 0xEC5400 */

void MMAP_$RELEASE_PAGES(uint16_t pid, uint32_t *vpn_array, uint16_t count)
{
    uint16_t wsl_index = MMAP_PID_TO_WSL[pid];

    for (uint16_t i = 0; i < count; i++) {
        uint32_t vpn = vpn_array[i];
        mmape_t *page = MMAPE_FOR_VPN(vpn);

        /* Only release if page belongs to this process's WSL,
         * is in a WSL, and has no wire count */
        if (page->wsl_index != wsl_index) continue;
        if (!(page->flags1 & MMAPE_FLAG1_IN_WSL)) continue;
        if (page->wire_count != 0) continue;

        /* Remove from current WSL */
        mmap_$remove_from_wsl(page, vpn);

        /* Determine destination list based on page type */
        uint16_t dest_type;
        uint16_t *pmape = PMAPE_FOR_VPN(vpn);

        if ((pmape[1] & PMAPE_FLAG_MODIFIED) || (page->flags2 & MMAPE_FLAG2_MODIFIED)) {
            /* Page is dirty - check segment for flush requirement */
            uint16_t seg = page->segment;
            void *seg_info = SEGMENT_TABLE[seg];

            boolean needs_flush;
            if (page->flags2 & MMAPE_FLAG2_ON_DISK) {
                /* Check segment's write-back requirement (offset 0x28) */
                needs_flush = (*(int16_t*)((char*)seg_info + 0x28)) != 0;
            } else {
                /* Check segment's flush flag (offset 0xB9 bit 7) */
                needs_flush = (*(int8_t*)((char*)seg_info + 0xB9)) < 0;
            }

            dest_type = needs_flush ? MMAP_PAGE_TYPE_DIRTY_FL : MMAP_PAGE_TYPE_DIRTY_NF;
        } else if (page->flags1 & MMAPE_FLAG1_IMPURE) {
            dest_type = MMAP_PAGE_TYPE_PURE;
        } else {
            dest_type = MMAP_PAGE_TYPE_IMPURE;
        }

        /* Add to appropriate free list */
        mmap_$add_to_wsl(page, vpn, dest_type, -1);
    }
}

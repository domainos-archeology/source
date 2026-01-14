/*
 * MMAP_$WS_SCAN - Scan working set for page replacement
 *
 * Scans a working set list to find pages that can be removed.
 * Used for page replacement when memory is needed. Pages are
 * categorized and moved to appropriate free lists.
 *
 * Original address: 0x00e0d364
 */

#include "mmap.h"
#include "misc/misc.h"
#include "mmu/mmu.h"

/* Segment info table */
extern void *SEGMENT_TABLE[];  /* At 0xEC5400 */

uint32_t MMAP_$WS_SCAN(uint16_t wsl_index, int16_t mode, uint32_t pages_needed, uint32_t param4)
{
    (void)param4;  /* Unused in decompilation */

    /* Validate WSL index */
    if (wsl_index < WSL_INDEX_MIN_USER || wsl_index > MMAP_WSL_HI_MARK) {
        CRASH_SYSTEM(Illegal_WSL_Index_Err);
    }

    MMAP_$WS_SCAN_CNT++;

    ws_hdr_t *wsl = WSL_FOR_INDEX(wsl_index);
    uint32_t scanned = 0;
    uint32_t removed = 0;

    /* Lists to collect freed pages by type */
    uint32_t free_list = 0;       /* Type 2: Clean, not impure */
    uint32_t pure_list = 0;       /* Type 1: Pure pages */
    uint32_t dirty_nf_list = 0;   /* Type 3: Dirty, no flush needed */
    uint32_t dirty_fl_list = 0;   /* Type 4: Dirty, needs flush */

    uint32_t current_vpn = wsl->head_vpn;
    uint32_t page_count = wsl->page_count;

    while (scanned < page_count && removed < pages_needed) {
        mmape_t *page = MMAPE_FOR_VPN(current_vpn);
        uint16_t *pmape = PMAPE_FOR_VPN(current_vpn);
        uint16_t next_vpn = page->prev_vpn;

        boolean should_remove = false;

        if (mode < 0) {
            /* Aggressive mode: check for modified/dirty pages */
            boolean is_dirty = (pmape[1] & PMAPE_FLAG_MODIFIED) ||
                               (page->flags2 & MMAPE_FLAG2_MODIFIED);

            if (is_dirty && !(page->flags2 & MMAPE_FLAG2_ON_DISK)) {
                /* Check segment info for removability */
                uint16_t seg = page->segment;
                void *seg_info = SEGMENT_TABLE[seg];
                if ((*(uint16_t*)((char*)seg_info + 0x0E)) & 0x1000) {
                    should_remove = true;
                }
            }
        } else {
            /* Normal mode: check referenced bit */
            if (pmape[1] & PMAPE_FLAG_REFERENCED) {
                /* Clear referenced bit */
                pmape[1] &= ~PMAPE_FLAG_REFERENCED;
            } else {
                should_remove = true;
            }
        }

        if (should_remove) {
            removed++;

            /* Unlink from WSL */
            uint16_t prev = page->next_vpn;
            uint16_t next = page->prev_vpn;
            MMAPE_FOR_VPN(prev)->prev_vpn = next;
            MMAPE_FOR_VPN(next)->next_vpn = prev;

            if (page->wire_count == 0) {
                /* Clear PTE and TLB entry */
                uint32_t pte_offset = ((uint32_t)page->seg_offset << 2) +
                                      ((uint32_t)page->segment << 7);
                uint16_t *pte = (uint16_t*)((char*)PTE_BASE + pte_offset - 0x80);
                if (*pte & 0x2000) {
                    *pte &= ~0x20;
                    MMU_$REMOVE(current_vpn);
                }

                /* Categorize page for free list */
                boolean is_dirty = (pmape[1] & PMAPE_FLAG_MODIFIED) ||
                                   (page->flags2 & MMAPE_FLAG2_MODIFIED);

                if (!is_dirty) {
                    if (page->flags1 & MMAPE_FLAG1_IMPURE) {
                        page->next_vpn = (uint16_t)pure_list;
                        pure_list = current_vpn;
                    } else {
                        page->next_vpn = (uint16_t)free_list;
                        free_list = current_vpn;
                    }
                } else {
                    /* Dirty page - check if needs flush */
                    uint16_t seg = page->segment;
                    void *seg_info = SEGMENT_TABLE[seg];

                    boolean needs_flush;
                    if (page->flags2 & MMAPE_FLAG2_ON_DISK) {
                        needs_flush = (*(int16_t*)((char*)seg_info + 0x28)) != 0;
                    } else {
                        needs_flush = (*(int16_t*)((char*)seg_info + 0x08)) < 0;
                    }

                    if (needs_flush) {
                        page->next_vpn = (uint16_t)dirty_fl_list;
                        dirty_fl_list = current_vpn;
                    } else {
                        page->next_vpn = (uint16_t)dirty_nf_list;
                        dirty_nf_list = current_vpn;
                    }
                }
            } else {
                /* Page is wired - just update stats */
                MMAP_$PAGEABLE_PAGES_LOWER_LIMIT--;
                page->flags1 &= ~MMAPE_FLAG1_IN_WSL;
            }
        }

        current_vpn = next_vpn;
        scanned++;
    }

    /* Update WSL statistics */
    wsl->page_count -= removed;
    if (scanned < wsl->scan_pos) {
        wsl->scan_pos -= scanned;
    } else {
        wsl->scan_pos = 0;
    }
    wsl->head_vpn = current_vpn;

    MMAP_$WS_REMOVE += removed;

    /* Move collected pages to their respective free lists */
    if (free_list != 0) {
        mmap_$move_pages_to_wsl_type(free_list, MMAP_PAGE_TYPE_IMPURE);
    }
    if (pure_list != 0) {
        mmap_$move_pages_to_wsl_type(pure_list, MMAP_PAGE_TYPE_PURE);
    }
    if (dirty_nf_list != 0) {
        mmap_$move_pages_to_wsl_type(dirty_nf_list, MMAP_PAGE_TYPE_DIRTY_NF);
    }
    if (dirty_fl_list != 0) {
        mmap_$move_pages_to_wsl_type(dirty_fl_list, MMAP_PAGE_TYPE_DIRTY_FL);
    }

    return scanned;
}

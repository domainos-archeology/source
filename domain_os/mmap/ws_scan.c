/*
 * MMAP_$WS_SCAN - Scan working set for page replacement
 *
 * Scans a working set list to find pages that can be removed.
 * Used for page replacement when memory is needed. Pages are
 * categorized and moved to appropriate free lists.
 *
 * Original address: 0x00e0d364
 */

#include "mmap/mmap_internal.h"
#include "misc/misc.h"
#include "mmu/mmu.h"

/*
 * Status cells passed to CRASH_SYSTEM by `pea (d,PC)`.
 *
 * These are constant longwords in this module's own code region, not
 * shared globals; the cell address is part of each name.  Names come from
 * the SR10.4 status-code database.
 */
/*
 * 0x00E0D382: pea (-0x9a4,PC) -> 0x00E0C9E0, jsr CRASH_SYSTEM at 0x00E0D386.
 * Shared with MMAP_$SET_WS_PRI, MMAP_$PURGE and MMAP_$SET_WS_INDEX.
 */
static const status_$t mmap_$illegal_wsl_index_00e0c9e0 = 0x00060009;

uint32_t MMAP_$WS_SCAN(uint16_t wsl_index, int16_t mode, uint32_t pages_needed, uint32_t param4)
{
    (void)param4;  /* Unused in decompilation */

    /* Validate WSL index */
    if (wsl_index < WSL_INDEX_MIN_USER || wsl_index > MMAP_WSL_HI_MARK) {
        CRASH_SYSTEM(&mmap_$illegal_wsl_index_00e0c9e0);
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
                /*
                 * The owning object's attribute flags decide whether the
                 * page may be taken:
                 *
                 *   00e0d424  move.w (-0x1ffe,A3),D1w  ; mmape->segment
                 *   00e0d428  movea.l #0xec5400,A1
                 *   00e0d42e  lsl.w #0x2,D1w
                 *   00e0d430  move.w D1w,D0w
                 *   00e0d432  lsl.w #0x2,D0w
                 *   00e0d434  add.w D0w,D1w            ; seg * 0x14
                 *   00e0d436  lea (0x0,A1,D1w),A1
                 *   00e0d43a  movea.l (-0x10,A1),A4    ; SEG_ASTE(seg)->aote
                 *   00e0d43e  move.w (0xe,A4),D0w      ; attribute flags word
                 *   00e0d442  btst.l #0xc,D0
                 */
                aote_t *aote = MMAP_$SEG_ASTE_FOR(page->segment)->aote;
                if (MMAP_AOTE_ATTR_FLAGS(aote) & MMAP_AOTE_ATTR_FLAG_BIT12) {
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
                    /*
                     * Dirty page - ask the owning object whether the write
                     * has to be flushed.  Both arms rebuild the same
                     * 0xEC5400 + seg * 0x14 address and read (-0x10,A0):
                     *
                     *   00e0d4da-00e0d4ec  seg * 0x14 (ON_DISK arm)
                     *   00e0d4f0  movea.l (-0x10,A0),A1  ; SEG_ASTE(seg)->aote
                     *   00e0d4f4  tst.w (0x28,A1) / sne  ; high word of
                     *                                      aote->len_high
                     *   00e0d4fc-00e0d50e  seg * 0x14 (not-ON_DISK arm)
                     *   00e0d512  movea.l (-0x10,A0),A1
                     *   00e0d516  tst.w (0x8,A1) / smi   ; sign of the high
                     *                                      word of vol_uid
                     */
                    aote_t *aote = MMAP_$SEG_ASTE_FOR(page->segment)->aote;

                    boolean needs_flush;
                    if (page->flags2 & MMAPE_FLAG2_ON_DISK) {
                        needs_flush = (aote->len_high >> 16) != 0;
                    } else {
                        needs_flush = (aote->vol_uid & 0x80000000u) != 0;
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
        mmap_$move_pages_to_wsl_type(free_list, MMAP_PAGE_TYPE_IMPURE,
                                     wsl_index, mode);
    }
    if (pure_list != 0) {
        mmap_$move_pages_to_wsl_type(pure_list, MMAP_PAGE_TYPE_PURE,
                                     wsl_index, mode);
    }
    if (dirty_nf_list != 0) {
        mmap_$move_pages_to_wsl_type(dirty_nf_list, MMAP_PAGE_TYPE_DIRTY_NF,
                                     wsl_index, mode);
    }
    if (dirty_fl_list != 0) {
        mmap_$move_pages_to_wsl_type(dirty_fl_list, MMAP_PAGE_TYPE_DIRTY_FL,
                                     wsl_index, mode);
    }

    return scanned;
}

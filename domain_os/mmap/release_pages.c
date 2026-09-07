/*
 * MMAP_$RELEASE_PAGES - Release pages for a process
 *
 * Processes an array of page numbers and releases those belonging
 * to the specified process. Each page is moved to an appropriate
 * free list based on its type (pure/impure/dirty).
 *
 * Original address: 0x00e0cff8
 */

#include "mmap/mmap_internal.h"

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
            /*
             * Page is dirty - ask the owning object whether the write has to
             * be flushed.  A4 holds 0xEC5400 for the whole loop (loaded at
             * 00e0d034); both arms index it by seg * 0x14 and read the
             * longword at -0x10, i.e. SEG_ASTE(seg)->aote:
             *
             *   00e0d0b0-00e0d0ba  seg * 0x14 (ON_DISK arm)
             *   00e0d0bc  lea (0x0,A4,D0w),A1
             *   00e0d0c0  movea.l (-0x10,A1),A0
             *   00e0d0c4  tst.w (0x28,A0) / sne  ; high word of aote->len_high
             *   00e0d0cc-00e0d0d6  seg * 0x14 (not-ON_DISK arm)
             *   00e0d0d8  lea (0x0,A4,D0w),A1
             *   00e0d0dc  movea.l (-0x10,A1),A0
             *   00e0d0e0  tst.b (0xb9,A0) / smi  ; aote->remote_flag < 0
             */
            aote_t *aote = MMAP_$SEG_ASTE_FOR(page->segment)->aote;

            boolean needs_flush;
            if (page->flags2 & MMAPE_FLAG2_ON_DISK) {
                needs_flush = (aote->len_high >> 16) != 0;
            } else {
                needs_flush = aote->remote_flag < 0;
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

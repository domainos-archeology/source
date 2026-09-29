/*
 * ast_$invalidate_no_wait - Mark an object's installed pages stale
 *                           without waiting
 *
 * A Pascal procedure nested inside AST_$INVALIDATE (0x00E0662E).  Its one
 * parameter is the end page; through the static link (`move.l (A6),
 * (-0x24,A6)` at 0x00E064BC) it reads the parent's AOTE (-0x14) and start
 * page (0xC), which arrive as explicit arguments here.
 *
 * Walks the AOTE's ASTE list (descending by segment) from the end page's
 * segment down to the start page's.  ASTEs above the current segment are
 * skipped; when the list jumps below it the walk follows.  An ASTE in
 * transition is waited for and the list restarted.  For an ASTE holding
 * pages, every installed page in range is, under the PMAP lock, moved to
 * the impure pool when its MMAPE's working-set index is 3 or 4 and it is
 * unwired, then has MODIFIED cleared in its MMAPE, bit 22 set in the
 * MMAPE's disk address and MODIFIED cleared in its PFT word.  The ASTE is
 * marked dirty and the AST in-transition eventcount advanced.
 *
 * Original address: 0x00E064B0 (382 bytes).  A5 is the parent's
 * (0xE1DC80; (0x428,A5) = AST_$AST_IN_TRANS_EC).
 */

#include "ast/ast_internal.h"
#include "mmu/mmu.h"
#include "mmap/mmap.h"

void ast_$invalidate_no_wait(uint32_t end_page, aote_t *aote,
                             uint32_t start_page)
{
    uint32_t start_seg;         /* D4 */
    uint32_t seg;               /* D3 */
    uint16_t last_in_seg;       /* D5w */
    uint16_t first_in_seg;      /* D2w */
    int16_t pages_left;         /* D2w in the page loop */
    aste_t *aste;               /* A4 */
    uint32_t *entry;            /* A3 */
    uint32_t ppn;               /* D6 */
    mmape_t *mmape;             /* A2 - 0x2000 */
    uint32_t *pft;

    /* 0x00E064B8..0x00E064D6 */
    start_seg = start_page >> 5;
    seg = end_page >> 5;
    last_in_seg = (uint16_t)(end_page & 0x1F);
    aste = aote->aste_list;

    while (aste != NULL) {
        /* 0x00E064DE..0x00E064E6: an ASTE above the current segment is
         * skipped (unsigned word compare) */
        if ((uint16_t)seg < aste->segment) {
            aste = aste->next;
            continue;
        }
        /* 0x00E064EA..0x00E064F4: the list jumped below - follow it and
         * take the whole segment */
        if (aste->segment < (uint16_t)seg) {
            seg = (seg & 0xFFFF0000u) | aste->segment;
            last_in_seg = 0x1F;
        }
        /* 0x00E064F6..0x00E0650A: done below the start segment; in the
         * start segment begin at the start page */
        first_in_seg = 0;
        if ((uint16_t)seg < (uint16_t)start_seg) {
            break;
        }
        if ((uint16_t)start_seg == (uint16_t)seg) {
            first_in_seg = (uint16_t)(start_page & 0x1F);
        }

        /* 0x00E0650C..0x00E06522: in transition - wait, restart the list */
        if ((int16_t)aste->flags < 0) {
            AST_$WAIT_FOR_AST_INTRANS();
            aste = aote->aste_list;
            continue;
        }

        /* 0x00E06526 */
        if (aste->page_count != 0) {
            /* 0x00E0652E..0x00E06554: bits 15 and 14, swap the locks */
            aste->flags |= ASTE_FLAG_IN_TRANS;
            aste->flags |= ASTE_FLAG_LOCKED;
            ML_$UNLOCK(AST_LOCK_ID);
            ML_$LOCK(PMAP_LOCK_ID);

            /* 0x00E06556..0x00E0656C: the entry of the first page */
            entry = (uint32_t *)((char *)PMAP_SEGMAP_ROW(aste->seg_index)
                                 + ((uint16_t)(first_in_seg << 2)));

            /* 0x00E06570..0x00E06576: pages first..last, upwards */
            pages_left = (int16_t)(last_in_seg - first_in_seg);
            if (pages_left >= 0) {
                do {
                    /* 0x00E0657A..0x00E06580 */
                    while ((int32_t)*entry < 0) {
                        ast_$wait_for_page_transition();
                    }

                    /* 0x00E06582..0x00E06588 */
                    if (*entry & SEGMAP_VALID) {
                        /* 0x00E0658A..0x00E065C0: wsl_index 3..4 and
                         * unwired -> impure transfer */
                        ppn = *entry & 0xFFFF;
                        mmape = &MMAPE_BASE[ppn];
                        if (mmape->wsl_index >= 3 && mmape->wsl_index <= 4 &&
                            mmape->wire_count == 0) {
                            MMAP_$IMPURE_TRANSFER(mmape, ppn);
                        }
                        /* 0x00E065C2: bclr.b #6 of +0x09 (flags2) */
                        mmape->flags2 &= (uint8_t)~MMAPE_FLAG2_MODIFIED;
                        /* 0x00E065C8: bset.b #6 of +0x0D = disk_addr bit 22 */
                        mmape->disk_addr |= 0x00400000u;
                        /* 0x00E065CE..0x00E065D8: PFT low word &= ~0x4000 */
                        pft = PFT_FOR_PPN(ppn);
                        *pft &= ~(uint32_t)PFT_FLAG_MODIFIED;
                    }

                    /* 0x00E065DE..0x00E065E0 */
                    entry++;
                } while (pages_left-- != 0);
            }

            /* 0x00E065E4..0x00E06616: dirty, locks back, clear bit 15
             * only, wake waiters */
            aste->flags |= ASTE_FLAG_DIRTY;
            ML_$UNLOCK(PMAP_LOCK_ID);
            ML_$LOCK(AST_LOCK_ID);
            aste->flags &= (uint16_t)~ASTE_FLAG_IN_TRANS;
            EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);
        }

        /* 0x00E06618..0x00E0661A: next ASTE, previous segment */
        aste = aste->next;
        seg = (seg & 0xFFFF0000u) | ((seg - 1) & 0xFFFF);   /* subq.w #1,D3w */
    }
}

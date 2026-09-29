/*
 * ast_$invalidate_with_wait - Remove an object's installed pages, waiting
 *                             for transitions
 *
 * A Pascal procedure nested inside AST_$INVALIDATE (0x00E0662E).  It has
 * one parameter of its own (the end page) and reaches four cells of the
 * parent frame through the static link (`move.l (A6),D6` at 0x00E06306):
 * the AOTE (-0x14), the start page (0xC), the is_remote flag (-0x1A) and
 * a longword at (-0xC) that it clears and nothing reads.  They arrive as
 * explicit arguments here.
 *
 * Walks the segments from the end page's DOWN to the start page's.  For
 * each segment the ASTE is looked up (and, for a remote object, created);
 * a segment with no ASTE is skipped.  With the ASTE marked in transition
 * and locked (bits 15 and 14 - only bit 15 is cleared afterwards) the
 * pages are walked downwards under the PMAP lock: an installed page whose
 * MMAPE is wired fails the whole call with "pages wired"; otherwise its
 * MMU mapping is dropped when wired, the entry takes the MMAPE's disk
 * address with bit 22 set, the frame is freed and the ASTE page count
 * goes down.  A non-installed page with a disk address just gets bit 22.
 * The ASTE is marked dirty and the AST in-transition eventcount advanced
 * after every segment.
 *
 * Original address: 0x00E062FA (438 bytes).  A5 is the parent's
 * (0xE1DC80; (0x428,A5) = AST_$AST_IN_TRANS_EC).
 * Frame: (0x8,A6) end page longword (D4); (-0x8) status; (-0x26) first
 * page in the segment.
 */

#include "ast/ast_internal.h"
#include "mmu/mmu.h"
#include "mmap/mmap.h"

status_$t ast_$invalidate_with_wait(uint32_t end_page, aote_t *aote,
                                    uint32_t start_page, int8_t is_remote,
                                    uint32_t *parent_scratch)
{
    status_$t local_status;     /* (-0x8,A6) */
    uint32_t page;              /* D4: the highest page still to do */
    uint32_t seg;               /* D2 */
    uint16_t last_in_seg;       /* D3w */
    uint16_t first_in_seg;      /* (-0x26,A6) */
    int16_t pages_left;         /* D5w */
    aste_t *aste;               /* A4 */
    uint32_t *entry;            /* A2 */
    uint32_t ppn;               /* D3 */
    mmape_t *mmape;             /* A3 - 0x2000 */

    /* 0x00E06302..0x00E06314 */
    local_status = status_$ok;
    page = end_page;
    seg = page >> 5;
    last_in_seg = (uint16_t)(page & 0x1F);

    for (;;) {
        /* 0x00E0631A..0x00E06354 */
        aste = ast_$lookup_aste(aote, (int16_t)seg);
        if (aste == NULL && is_remote < 0) {
            aste = ast_$lookup_or_create_aste(aote, (uint16_t)seg,
                                              &local_status);
            if (aste == NULL) {
                goto done;
            }
        }

        /* 0x00E06358..0x00E06366: the first page of this segment's run
         * is the segment base, or the start page when that is higher
         * (signed compare) */
        page -= last_in_seg;
        if ((int32_t)page < (int32_t)start_page) {
            page = start_page;
        }

        /* 0x00E0636A: no ASTE - nothing installed here */
        if (aste == NULL) {
            goto next_segment;
        }

        /* 0x00E06372..0x00E06398: bits 15 and 14, swap the locks */
        aste->flags |= ASTE_FLAG_IN_TRANS;
        aste->flags |= ASTE_FLAG_LOCKED;
        ML_$UNLOCK(AST_LOCK_ID);
        ML_$LOCK(PMAP_LOCK_ID);

        /* 0x00E0639A..0x00E063AC: the entry of the LAST page in the run */
        entry = (uint32_t *)((char *)PMAP_SEGMAP_ROW(aste->seg_index)
                             + ((uint16_t)(last_in_seg << 2)));

        /* 0x00E063B0..0x00E063B2: the parent's (-0xC) cell */
        *parent_scratch = 0;

        /* 0x00E063B6..0x00E063C8: pages first_in_seg..last_in_seg,
         * walked downwards; an empty run skips the loop */
        first_in_seg = (uint16_t)(page & 0x1F);
        pages_left = (int16_t)(last_in_seg - first_in_seg);
        if (pages_left >= 0) {
            do {
                /* 0x00E063CC..0x00E063D2 */
                while ((int32_t)*entry < 0) {
                    ast_$wait_for_page_transition();
                }

                /* 0x00E063D4..0x00E063DA: btst.l #0xe on the high word */
                if (*entry & SEGMAP_VALID) {
                    /* 0x00E063DC..0x00E063F4 */
                    ppn = *entry & 0xFFFF;
                    mmape = &MMAPE_BASE[ppn];
                    if (mmape->wire_count != 0) {
                        /* 0x00E063F6..0x00E063FE */
                        local_status = status_$pmap_pages_wired;
                        break;
                    }
                    /* 0x00E06400..0x00E06414: bit 29 -> drop the mapping */
                    if (*entry & SEGMAP_WIRED) {
                        *entry &= ~SEGMAP_WIRED;
                        MMU_$REMOVE(ppn);
                    }
                    /* 0x00E06416..0x00E06426: clear bit 30, take the
                     * disk address, set bit 22 (bset.b #6,(0x1,A2)) */
                    *entry &= ~SEGMAP_VALID;
                    *entry &= 0xFF800000u;
                    *entry |= mmape->disk_addr;
                    *entry |= 0x00400000u;
                    /* 0x00E0642C..0x00E0643A */
                    MMAP_$FREE_REMOVE(mmape, ppn);
                    aste->page_count--;
                } else {
                    /* 0x00E06440..0x00E0644A: on disk -> bit 22 */
                    if ((*entry & 0x3FFFFF) != 0) {
                        *entry |= 0x00400000u;
                    }
                }

                /* 0x00E06450..0x00E06452: subq.l #0x4,A2 / dbf */
                entry--;
            } while (pages_left-- != 0);
        }

        /* 0x00E06456..0x00E06488: dirty, swap the locks back, clear bit
         * 15 only, wake waiters */
        aste->flags |= ASTE_FLAG_DIRTY;
        ML_$UNLOCK(PMAP_LOCK_ID);
        ML_$LOCK(AST_LOCK_ID);
        aste->flags &= (uint16_t)~ASTE_FLAG_IN_TRANS;
        EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);

        /* 0x00E0648A */
        if (local_status != status_$ok) {
            goto done;
        }

next_segment:
        /* 0x00E06490..0x00E0649E: stop once the run reached the start
         * page (unsigned); else the previous segment, all of it */
        if (page <= start_page) {
            goto done;
        }
        page -= 1;
        last_in_seg = 0x1F;
        seg = (seg & 0xFFFF0000u) | ((seg - 1) & 0xFFFF);   /* subq.w #1,D2w */
    }

done:
    /* 0x00E064A2 */
    return local_status;
}

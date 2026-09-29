/*
 * AST_$PMAP_ASSOC - Associate a physical page with a page of a segment
 *
 * Works on the segment map entry for (aste, page).  If the entry already
 * holds an installed page, that page is torn down first: its wired
 * mapping removed, and - unless its MMAPE is wired, which fails the call
 * with "pages wired" - the entry takes the MMAPE's disk address, the
 * frame is returned with MMAP_$FREE_REMOVE and the ASTE page count goes
 * down.  An entry with neither a page nor a disk address on a LOCAL
 * object reports "bad assoc" and, unless `allow_no_disk` is TRUE, stops
 * there.  Then, for a real page number (0x200..0xFFF), the new MMAPE is
 * filled in and the page installed in the working set when it is not
 * wired.  Finally the entry records the new ppn as installed and wired
 * (bits 30 and 29), the PFT word is marked, and the page count goes up.
 *
 * Parameters (frame at 0x00E042B0, `link.w A6,-0x10`):
 *   aste          (0x08,A6)  (D5)
 *   page          (0x0C,A6)  word (D2)
 *   ppn           (0x0E,A6)  longword (D3); its ADDRESS goes to
 *                            MMAP_$INSTALL_LIST
 *   flags1        (0x12,A6)  a word the routine never reads
 *   allow_no_disk (0x14,A6)  read as ONE BYTE (`move.b (0x14,A6),D4b`) and
 *                            tested with tst.b/bpl: a Domain boolean.  The
 *                            prototype keeps the callers' uint16_t shape;
 *                            the body tests the low byte's sign, which is
 *                            what the C callers set (0x00FF / 0).
 *                            TODO(source-jhc6): the image reads the HIGH
 *                            byte of the slot (`st -(SP)` pushes there).
 *   status        (0x16,A6)  (A4), cleared first
 *
 * Original address: 0x00E042B0 (414 bytes), A5 = 0xE1DC80 (unused here).
 */

#include "ast/ast_internal.h"
#include "misc/misc.h"
#include "mmu/mmu.h"
#include "mmap/mmap.h"

/*
 * Status cells passed to CRASH_SYSTEM by `pea (d,PC)`.
 */
/*
 * 0x00E04396: pea (-0x34ec,PC) -> 0x00E00EAC, jsr CRASH_SYSTEM at
 * 0x00E0439A.  Image bytes 00 05 00 03 (pmap "mismatch").  Shared with
 * AST_$ASSOC_AREA, ast_$allocate_pages and AST_$TOUCH.
 */
static const status_$t pmap_$mismatch_00e00eac = 0x00050003;
/*
 * 0x00E043C6: pea (-0xe84,PC) -> 0x00E03544, jsr CRASH_SYSTEM at
 * 0x00E043CA.  Image bytes 00 06 00 0C (mmap "bad install").  Shared with
 * AST_$ASSOC_AREA, AST_$COPY_AREA and AST_$TOUCH.
 */
static const status_$t mmap_$bad_install_00e03544 = 0x0006000C;

void AST_$PMAP_ASSOC(aste_t *aste, uint16_t page, uint32_t ppn,
                     uint16_t flags1, uint16_t allow_no_disk, status_$t *status)
{
    uint32_t *entry;            /* A3 - 0x80 */
    mmape_t *mmape;             /* A2 - 0x2000 */
    uint32_t old_ppn;           /* D4 */
    uint32_t *pft;

    (void)flags1;

    /* 0x00E042D2 */
    *status = status_$ok;

    /* 0x00E042D4..0x00E042F0: 0xED5000 + (seg_index << 7) + (page << 2,
     * 16-bit) - 0x80 */
    entry = (uint32_t *)((char *)PMAP_SEGMAP_ROW(aste->seg_index)
                         + (uint16_t)(page << 2));

    /* 0x00E042F4..0x00E042FC: wait while in transition */
    while ((int32_t)*entry < 0) {
        ast_$wait_for_page_transition();
    }

    /* 0x00E042FE..0x00E04306: btst.l #0xe on the high word = bit 30 */
    if (*entry & SEGMAP_VALID) {
        /* 0x00E04308..0x00E04318 */
        old_ppn = *entry & 0xFFFF;
        mmape = &MMAPE_BASE[old_ppn];

        /* 0x00E0431C..0x00E04330: bit 29 -> unmap */
        if (*entry & SEGMAP_WIRED) {
            *entry &= ~SEGMAP_WIRED;                /* bclr.b #0x5 */
            MMU_$REMOVE(old_ppn);
        }

        /* 0x00E04332..0x00E0433E: a wired frame cannot be replaced */
        if (mmape->wire_count != 0) {
            *status = status_$pmap_pages_wired;     /* 0x50007 */
            return;
        }

        /* 0x00E04342..0x00E04354: clear bit 30, keep the top nine bits,
         * take the MMAPE's disk address */
        *entry &= ~SEGMAP_VALID;
        *entry &= 0xFF800000u;
        *entry |= mmape->disk_addr;

        /* 0x00E04358..0x00E04368 */
        MMAP_$FREE_REMOVE(mmape, old_ppn);
        aste->page_count--;
    } else {
        /* 0x00E0436E..0x00E0438E: no page and no disk address on a local
         * object is "bad assoc"; only a TRUE allow flag goes on */
        if ((*entry & 0x7FFFFF) == 0 && aste->aote->remote_flag >= 0) {
            *status = status_$pmap_bad_assoc;       /* 0x50006 */
            if ((int8_t)allow_no_disk >= 0) {
                return;
            }
        }
    }

    /* 0x00E04392..0x00E043A0 */
    if (ppn == 0) {
        CRASH_SYSTEM(&pmap_$mismatch_00e00eac);
    }

    /* 0x00E043A2..0x00E043B0: unsigned 0x200 <= ppn <= 0xFFF */
    if (ppn >= 0x200 && ppn <= 0xFFF) {
        mmape = &MMAPE_BASE[ppn];

        /* 0x00E043C0..0x00E043D0: flags1 bit 7 = already in a WSL */
        if ((int8_t)mmape->flags1 < 0) {
            CRASH_SYSTEM(&mmap_$bad_install_00e03544);
        }

        /* 0x00E043D2..0x00E043FA */
        mmape->segment = aste->seg_index;
        mmape->flags1 |= MMAPE_FLAG1_IMPURE;        /* bset.b #6,+0x05 */
        mmape->seg_offset = (uint8_t)page;
        mmape->flags2 |= MMAPE_FLAG2_MODIFIED;      /* bset.b #6,+0x09 */
        mmape->flags2 &= (uint8_t)~MMAPE_FLAG2_ON_DISK; /* bclr.b #7,+0x09 */
        mmape->disk_addr = *entry & 0x7FFFFF;

        /* 0x00E043FE..0x00E0440E: an unwired frame joins the working set;
         * `move.l #0x10000` is count 1 and use_wired 0 */
        if (mmape->wire_count == 0) {
            MMAP_$INSTALL_LIST(&ppn, 1, 0);
        }
    }

    /* 0x00E04414..0x00E04418: low word := ppn, bit 30 */
    *entry = (*entry & 0xFFFF0000u) | (ppn & 0xFFFF);
    *entry |= SEGMAP_VALID;

    /* 0x00E0441E..0x00E04434: PFT low word: clear bit 14, set bit 13 */
    pft = PFT_FOR_PPN(ppn);
    *pft = (*pft & 0xFFFFBFFFu) | 0x00002000u;

    /* 0x00E04438..0x00E04440: bit 29 (wired), page count up */
    *entry |= SEGMAP_WIRED;
    aste->page_count++;
}

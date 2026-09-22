/*
 * AST_$ASSOC_AREA - Associate a physical page with an area segment page
 *
 * Works directly on the segment map entry for (seg_index, page).  If the
 * entry already holds an installed page, that page is torn down first:
 * its wired mapping is removed, its MMAPE's wire count is remembered, the
 * disk address is copied back from the MMAPE into the entry, the page is
 * returned with MMAP_$FREE_REMOVE and the segment's ASTE page count goes
 * down.  Then, for a real page number (0x200..0xFFF), the new MMAPE is
 * filled in and the page installed in the working set when the old page
 * was unwired.  Finally the entry records the new ppn as installed, the
 * PFT word for the ppn is marked, and the ASTE page count goes up.
 *
 * Parameters (frame at 0x00E04542, `link.w A6,-0x14`):
 *   seg_index (0x8,A6)  word (D3)
 *   page      (0xA,A6)  word (D4)
 *   ppn       (0xC,A6)  longword (D2); its address is what
 *                       MMAP_$INSTALL_LIST receives
 *   status    (0x10,A6)
 * (-0x6,A6) is the old page's wire count, a word.
 *
 * Original address: 0x00E04542 (390 bytes), A5 = 0xE1DC80 (unused here).
 */

#include "ast/ast_internal.h"
#include "misc/misc.h"
#include "mmu/mmu.h"
#include "mmap/mmap.h"

/*
 * Status cells passed to CRASH_SYSTEM by `pea (d,PC)`.  Both are literals
 * in the AST code region shared with other AST routines.
 */
/*
 * 0x00E04602: pea (-0x3758,PC) -> 0x00E00EAC, jsr CRASH_SYSTEM at
 * 0x00E04606.  Image bytes 00 05 00 03 (pmap "mismatch").  Shared with
 * AST_$PMAP_ASSOC, ast_$allocate_pages and AST_$TOUCH.
 */
static const status_$t pmap_$mismatch_00e00eac = 0x00050003;
/*
 * 0x00E04632: pea (-0x10f0,PC) -> 0x00E03544, jsr CRASH_SYSTEM at
 * 0x00E04636.  Image bytes 00 06 00 0C (mmap "bad install").  Shared with
 * AST_$PMAP_ASSOC and AST_$TOUCH.
 */
static const status_$t mmap_$bad_install_00e03544 = 0x0006000C;

void AST_$ASSOC_AREA(uint16_t seg_index, int16_t page, uint32_t ppn,
                     status_$t *status)
{
    uint32_t *entry;            /* A2-0x80: the segment map longword */
    mmape_t *mmape;             /* A3 (biased by 0x2000 in the image) */
    uint32_t old_ppn;           /* D5 */
    uint16_t old_wire_count;    /* (-0x6,A6) */
    uint32_t *pft;

    /*
     * 0x00E04558..0x00E04574: 0xED5000 + (seg << 7) with seg zero-extended
     * to 32 bits, plus (page << 2) as a 16-bit index, minus 0x80: the
     * map is 1-based by segment.
     */
    entry = (uint32_t *)((char *)SEGMAP_BASE + ((uint32_t)seg_index << 7) +
                         (int16_t)(page << 2) - 0x80);

    /* 0x00E04578..0x00E04580: wait while the entry is in transition */
    while ((int32_t)*entry < 0) {
        ast_$wait_for_page_transition();
    }

    /* 0x00E04582 */
    old_wire_count = 0;

    /* 0x00E04586..0x00E0458E: btst.l #0xe of the high word = bit 30 */
    if (*entry & SEGMAP_VALID) {
        /* 0x00E04590..0x00E045A0: the installed page's MMAPE */
        old_ppn = *entry & 0xFFFF;
        mmape = &MMAPE_BASE[old_ppn];

        /* 0x00E045A4..0x00E045B8: bit 29 (wired) -> clear it, unmap */
        if (*entry & SEGMAP_WIRED) {
            *entry &= ~SEGMAP_WIRED;            /* bclr.b #0x5,(-0x80,A2) */
            MMU_$REMOVE(old_ppn);
        }

        /* 0x00E045BA: bclr.b #0x6,(-0x80,A2) */
        *entry &= ~SEGMAP_VALID;

        /* 0x00E045C0..0x00E045C6: zero-extended wire count */
        old_wire_count = mmape->wire_count;

        /* 0x00E045CA..0x00E045D6: keep the flag bits, take the disk address */
        *entry &= 0xFF800000;
        *entry |= mmape->disk_addr;

        /* 0x00E045DA..0x00E045E6 */
        MMAP_$FREE_REMOVE(mmape, old_ppn);

        /* 0x00E045E8..0x00E045FA: subq.b #1 at 0xEC5400 + seg*0x14 - 4,
         * the page_count of the 1-based ASTE for this segment */
        ASTE_BASE[seg_index - 1].page_count--;
    }

    /* 0x00E045FE..0x00E0460C */
    if (ppn == 0) {
        CRASH_SYSTEM(&pmap_$mismatch_00e00eac);
    }

    /* 0x00E0460E..0x00E0461C: unsigned 0x200 <= ppn <= 0xFFF */
    if (ppn >= 0x200 && ppn <= 0xFFF) {
        mmape = &MMAPE_BASE[ppn];

        /* 0x00E0462C..0x00E0463C: tst.b (-0x1ffb,A3) = flags1 bit 7 */
        if ((int8_t)mmape->flags1 < 0) {
            CRASH_SYSTEM(&mmap_$bad_install_00e03544);
        }

        /* 0x00E0463E..0x00E04662 */
        mmape->wire_count = (uint8_t)old_wire_count;    /* move.b (-0x5,A6) */
        mmape->segment = seg_index;
        mmape->flags1 |= MMAPE_FLAG1_IMPURE;            /* bset.b #0x6 */
        mmape->seg_offset = (uint8_t)page;
        /* ori.w #0xc0,(-0x1ff8,A3): the priority/flags2 word; 0xC0 lands
         * in its low byte, flags2 = ON_DISK | MODIFIED */
        mmape->flags2 |= (MMAPE_FLAG2_ON_DISK | MMAPE_FLAG2_MODIFIED);
        mmape->disk_addr = *entry & 0x7FFFFF;

        /* 0x00E04666..0x00E04676: move.l #0x10000 = count 1, use_wired 0 */
        if (old_wire_count == 0) {
            MMAP_$INSTALL_LIST(&ppn, 1, 0);
        }
    }

    /* 0x00E0467C..0x00E04680: low word := ppn, bit 30 := installed */
    *entry = (*entry & 0xFFFF0000) | (ppn & 0xFFFF);
    *entry |= SEGMAP_VALID;

    /* 0x00E04686..0x00E0469C: PFT low word: clear bit 14, set bit 13 */
    pft = PFT_FOR_PPN(ppn);
    *pft = (*pft & 0xFFFFBFFF) | 0x00002000;

    /* 0x00E046A0..0x00E046B4: addq.b #1 to the same ASTE page_count */
    ASTE_BASE[seg_index - 1].page_count++;

    /* 0x00E046B8..0x00E046BC */
    *status = status_$ok;
}

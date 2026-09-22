/*
 * AST_$RELEASE_PAGES - Unwire a segment's pages
 *
 * For each of the segment's 32 map entries that is installed AND wired
 * (bits 30 and 29): the wired bit is cleared and, when the MMAPE is not
 * wired, the frame is collected; a frame that is still wired in the MMAPE
 * is unmapped at once with MMU_$REMOVE.  The collected frames are removed
 * from the MMU in one MMU_$REMOVE_LIST and, when `return_to_pool` is
 * TRUE, handed to MMAP_$RELEASE_PAGES for the current process.
 *
 * Parameters (frame at 0x00E06F88, `link.w A6,-0x90`):
 *   aste           (0x8,A6)
 *   return_to_pool (0xC,A6)  a single BOOLEAN byte (D2b)
 * (-0x80,A6) is the 32-longword collection (A4 = A6, stores at
 * (-0x84,A4) after `addq.l #4`, so 0-based).
 *
 * Original address: 0x00E06F88 (200 bytes), A5 = 0xE1DC80 (unused here).
 */

#include "ast/ast_internal.h"
#include "mmu/mmu.h"
#include "mmap/mmap.h"

void AST_$RELEASE_PAGES(aste_t *aste, int8_t return_to_pool)
{
    uint32_t *entry;            /* A2 */
    uint16_t count;             /* D3w */
    uint32_t ppn;               /* D5 */
    uint32_t list[32];          /* (-0x80,A6) */
    int16_t i;

    /* 0x00E06F9E..0x00E06FAE: 0xED5000 + (seg_index << 7) - 0x80 */
    count = 0;
    entry = (uint32_t *)((char *)SEGMAP_BASE +
                         ((uint32_t)aste->seg_index << 7) - 0x80);

    /* 0x00E06FB2..0x00E06FBE */
    ML_$LOCK(PMAP_LOCK_ID);

    /* 0x00E06FC0..0x00E0700A: moveq #0x1f / dbf = 32 entries */
    for (i = 0x1F; i >= 0; i--) {
        /* btst.l #0xe / #0xd on the high word = bits 30 and 29 */
        if ((*entry & SEGMAP_VALID) && (*entry & SEGMAP_WIRED)) {
            ppn = *entry & 0xFFFF;
            if (MMAPE_BASE[ppn].wire_count == 0) {
                /* 0x00E06FEC..0x00E06FF4 */
                *entry &= ~SEGMAP_WIRED;        /* bclr.b #0x5,(A2) */
                list[count] = ppn;
                count++;
            } else {
                /* 0x00E06FFA..0x00E07006 */
                *entry &= ~SEGMAP_WIRED;
                MMU_$REMOVE(ppn);
            }
        }
        entry++;
    }

    /* 0x00E0700E..0x00E07038 */
    if (count != 0) {
        MMU_$REMOVE_LIST(list, count);
        if (return_to_pool < 0) {
            MMAP_$RELEASE_PAGES(PROC1_$CURRENT, list, count);
        }
    }

    /* 0x00E0703A..0x00E07040 */
    ML_$UNLOCK(PMAP_LOCK_ID);
}

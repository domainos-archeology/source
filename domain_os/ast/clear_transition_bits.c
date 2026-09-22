/*
 * ast_$clear_transition_bits - Clear the in-transition bit on a run of
 *                              segment map entries
 *
 * Walks `count` consecutive segment map longwords.  For each entry that
 * holds an installed page (bit 30) whose page number lies in the real-page
 * range 0x200..0xFFF, the page is handed back to the replacement pool with
 * MMAP_$AVAIL; then bit 31 (in transition) is cleared.  Finally the PMAP
 * in-transition eventcount is advanced so waiters re-examine their entries.
 *
 * Parameters (frame at 0x00E0283C, `link.w A6,-0xc`):
 *   segmap (0x8,A6)  first segment map entry (A2)
 *   count  (0xC,A6)  number of entries, word; zero skips the loop
 *
 * A5 is inherited from the caller (no `lea` here); every caller is AST
 * code with A5 = 0xE1DC80, so (0x44C,A5) is AST_$PMAP_IN_TRANS_EC at
 * 0xE1E0CC.
 *
 * Original address: 0x00E0283C (92 bytes).
 */

#include "ast/ast_internal.h"

void ast_$clear_transition_bits(uint32_t *segmap, uint16_t count)
{
    uint32_t *entry;        /* A2 */
    int16_t remaining;      /* D2 */
    uint16_t ppn;           /* D3w */

    /* 0x00E02844..0x00E0284C: a zero count goes straight to the advance */
    entry = segmap;
    if (count != 0) {
        /* 0x00E0284E..0x00E02850: dbf counter = count - 1 */
        remaining = (int16_t)(count - 1);
        do {
            /* 0x00E02852..0x00E02858: btst.l #0xe on the high word = bit 30 */
            if (*entry & SEGMAP_VALID) {
                /* 0x00E0285A..0x00E02878: low word is the ppn; only real
                 * pages (0x200..0xFFF, unsigned) are made available */
                ppn = (uint16_t)(*entry & 0xFFFF);
                if (ppn >= 0x200 && ppn <= 0xFFF) {
                    MMAP_$AVAIL((uint32_t)ppn);         /* clr.l D1 / move.w */
                }
            }
            /* 0x00E0287A..0x00E0287E: bclr.b #0x7,(A2)+ / addq.l #3 = bit 31 */
            *entry &= ~SEGMAP_IN_TRANS;
            entry++;
            /* 0x00E02880: dbf D2w */
        } while (remaining-- != 0);
    }

    /* 0x00E02884..0x00E0288E: EC_$ADVANCE(&AST_$PMAP_IN_TRANS_EC) */
    EC_$ADVANCE(&AST_$PMAP_IN_TRANS_EC);
}

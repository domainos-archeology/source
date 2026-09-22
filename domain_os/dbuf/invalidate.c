/*
 * DBUF_$INVALIDATE - Drop one block, or every block, of a volume
 *
 * 0x00E3A9EC - 0x00E3AAA0 (182 bytes, A5 = 0xE78B58).  Verified against
 * the disassembly on 2026-09-19; the earlier emission was faithful, this
 * one goes through the VA link cells.
 *
 * Arguments:
 *   (0x8,A6) block    longword (D2); 0 means every block of the volume
 *   (0xc,A6) vol_idx  word (D3)
 *
 * Walks DBUF[0 .. dbuf_$count-1] in array order (A3 = &entry.block,
 * stride 0x24).  A matching entry has its volume nibble cleared, block
 * set to -1, busy cleared under the spin lock (waking waiters), dirty
 * cleared and ref_count zeroed; a specific block stops the walk at its
 * first match.  The volume's DBUF_$TROUBLE bit is cleared either way.
 */

#include "dbuf/dbuf_internal.h"

void DBUF_$INVALIDATE(int32_t block, uint16_t vol_idx)
{
    ml_$spin_token_t token;         /* (-0x2,A6) */
    int16_t n;                      /* D4 */
    dbuf_$entry_t *e;               /* A2 / A3 */
    uint16_t i;

    /* 0x00E3AA02 - 0x00E3AA0C */
    n = (int16_t)(dbuf_$count - 1);
    if (n >= 0) {
        for (i = 0; i <= (uint16_t)n; i++) {
            e = &DBUF[i];
            /* 0x00E3AA14 - 0x00E3AA24 */
            if (DBUF_GET_VOL(e) != vol_idx) {
                continue;
            }
            if (block != 0 && e->block != block) {
                continue;
            }
            /* 0x00E3AA26 - 0x00E3AA2E */
            e->flags &= 0xF0;
            e->block = -1;
            /* 0x00E3AA30 - 0x00E3AA76 */
            token = ML_$SPIN_LOCK(&DBUF_SPIN_LOCK);
            e->flags &= (uint8_t)~DBUF_ENTRY_BUSY;
            if (dbuf_$waiters != 0) {
                ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);
                EC_$ADVANCE(&dbuf_$eventcount);
            } else {
                ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);
            }
            /* 0x00E3AA78 - 0x00E3AA84 */
            e->flags &= (uint8_t)~DBUF_ENTRY_DIRTY;
            e->ref_count = 0;
            if (block != 0) {
                break;
            }
        }
    }

    /* 0x00E3AA8E - 0x00E3AA94: `bset.l D3,D0` on a cleared word, so the
     * bit number is taken modulo 32 and a vol_idx >= 16 clears nothing */
    DBUF_$TROUBLE &= (uint16_t)~(uint16_t)(1u << (vol_idx & 0x1F));
}

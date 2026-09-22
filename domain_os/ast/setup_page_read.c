/*
 * ast_$setup_page_read - Give a run of fresh pages their disk blocks
 *
 * Called with the PMAP lock held for `count` consecutive map entries that
 * have no page and no disk address yet.  Nothing is done for an object
 * whose attribute-flags bit 1 is set.  For an AREA object (attribute
 * flags bit 12) the blocks are allocated at once with BAT_$ALLOCATE, the
 * hint being the block of the previous entry (its MMAPE's when installed)
 * or the segment's file-map block >> 4 when there is none; each entry
 * then gets its block and bit 22.  For any other object BAT_$RESERVE
 * only reserves the count and each entry gets bit 22 with its address
 * cleared.  A BAT failure sets bit 31 of the status and leaves the map
 * alone.  Afterwards the ASTE is dirtied, the object length is extended
 * to cover the run (stamping DTA and DTM) or - when it already did and
 * flags bit 6 is clear - the AOTE is marked TOUCHED, and the block count
 * at aote+0x24 grows by `count`.
 *
 * Parameters (frame at 0x00E02898, `link.w A6,-0x9c`):
 *   aste       (0x08,A6)  (D7)
 *   segmap     (0x0C,A6)  (A2) the first entry of the run
 *   start_page (0x10,A6)  word (D2)
 *   count      (0x12,A6)  word (D3)
 *   flags      (0x14,A6)  word (D4); only bit 6 is read
 *   status     (0x16,A6)  (A3)
 * Locals: (-0x98) aote, (-0x8C) the allocation hint, (-0x88) the block
 * array BAT_$ALLOCATE fills (32 longwords fit below the aote cell).
 *
 * Original address: 0x00E02898 (606 bytes).  No A5.
 */

#include "ast/ast_internal.h"
#include "mmap/mmap.h"
#include "bat/bat.h"

void ast_$setup_page_read(aste_t *aste, uint32_t *segmap, uint16_t start_page,
                          uint16_t count, uint16_t flags, status_$t *status)
{
    aote_t *aote;               /* (-0x98,A6) */
    uint32_t hint;              /* (-0x8C,A6) */
    uint32_t blocks[32];        /* (-0x88,A6) */
    uint32_t prev;              /* (A0) = segmap - 4 */
    uint32_t *ent;              /* A2 */
    int32_t end_offset;         /* D0 */
    int16_t i;

    /* 0x00E028B4..0x00E028CE */
    aote = aste->aote;
    if (aote->attr_flags_lo & 0x02) {
        *status = status_$ok;
        return;
    }

    /* 0x00E028D2..0x00E028DA: move.w (0xe,A1) / btst.l #0xc = bit 4 of
     * attr_flags_hi */
    if (aote->attr_flags_hi & 0x10) {
        /*
         * 0x00E028DE..0x00E0292A: the hint.  Page 0, or a previous entry
         * with no disk address, uses the file-map block >> 4; an
         * installed previous entry uses its MMAPE's disk address; else
         * the entry's own.
         */
        if (start_page == 0) {
            /* 0x00E028E4 tst.w D2w / beq: page 0 never reads segmap[-1] */
            hint = aste->fm_block >> 4;
        } else {
            prev = segmap[-1];
            if ((prev & 0x3FFFFF) == 0) {
                hint = aste->fm_block >> 4;
            } else if (prev & SEGMAP_VALID) {
                hint = MMAPE_BASE[prev & 0xFFFF].disk_addr & 0x3FFFFF;
            } else {
                hint = prev & 0x3FFFFF;
            }
        }

        /* 0x00E0292E..0x00E0296E: BAT_$ALLOCATE(vol, hint, count, 0,
         * blocks, status) with the PMAP lock released */
        ML_$UNLOCK(PMAP_LOCK_ID);
        BAT_$ALLOCATE((int16_t)aote->vol_index, hint, (int16_t)count, 0,
                      blocks, status);
        ML_$LOCK(PMAP_LOCK_ID);

        /* 0x00E02970 */
        if (*status != status_$ok) {
            goto failed;                            /* 0x00E02A38 */
        }

        /* 0x00E02976..0x00E02994: bit 22, address := block */
        ent = segmap;
        for (i = 0; i < (int16_t)count; i++) {
            *ent |= 0x00400000u;                    /* bset.b #6,(0x1,A2) */
            *ent &= 0xFFC00000u;
            *ent |= blocks[i];
            ent++;
        }

        /* 0x00E02998..0x00E029F0: one log record per page (skipped when
         * count is 0): kind 9, page, block high and low words, 1, 0 */
        if (NETLOG_$OK_TO_LOG < 0 && count != 0) {
            for (i = 0; i < (int16_t)count; i++) {
                NETLOG_$LOG_IT(9, (uint32_t *)&aote->uid, aste->segment,
                               (uint16_t)(start_page + i),
                               (uint16_t)(blocks[i] >> 16),
                               (uint16_t)(blocks[i] & 0xFFFF), 1, 0);
            }
        }
    } else {
        /* 0x00E029F8..0x00E02A32: BAT_$RESERVE(vol, count, status) */
        ML_$UNLOCK(PMAP_LOCK_ID);
        BAT_$RESERVE((int16_t)aote->vol_index, (uint32_t)count, status);
        ML_$LOCK(PMAP_LOCK_ID);

        /* 0x00E02A34 */
        if (*status != status_$ok) {
            goto failed;
        }

        /* 0x00E02A40..0x00E02A52: bit 22, address cleared */
        ent = segmap;
        for (i = 0; i < (int16_t)count; i++) {
            *ent |= 0x00400000u;
            *ent &= 0xFFC00000u;
            ent++;
        }

        /* 0x00E02A56..0x00E02A80: kind 9, start page, 0, 0, count, 0 */
        if (NETLOG_$OK_TO_LOG < 0) {
            NETLOG_$LOG_IT(9, (uint32_t *)&aote->uid, aste->segment,
                           start_page, 0, 0, count, 0);
        }
    }

    /* 0x00E02A84..0x00E02A86: bset.b #0x5,(0x12,A1) = bit 13 */
    aste->flags |= ASTE_FLAG_DIRTY;

    /* 0x00E02A8C..0x00E02AAE: byte offset of the run's last page, signed
     * compare against the length */
    end_offset = (int32_t)(((uint32_t)start_page + (uint32_t)count - 1 +
                            ((uint32_t)aste->segment << 5)) << 10);
    if (end_offset < (int32_t)aote->length) {
        /* 0x00E02AD6..0x00E02ADC */
        if ((flags & 0x40) == 0) {
            aote->flags |= AOTE_FLAG_TOUCHED;
        }
    } else {
        /* 0x00E02AB0..0x00E02ACE: extend, stamp DTA, DTM := DTA */
        aote->length = (uint32_t)end_offset + 0x400;
        TIME_$CLOCK((clock_t *)&aote->dta_high);
        aote->dtm_high = aote->dta_high;
        aote->dtm_low = aote->dta_low;
    }

    /* 0x00E02AE2..0x00E02AE6 */
    aote->unknown_24 += (uint32_t)count;
    aote->flags |= AOTE_FLAG_DIRTY;
    return;

failed:
    /* 0x00E02A38: bset.b #0x7,(A3) = bit 31 */
    *status |= (status_$t)0x80000000u;
}

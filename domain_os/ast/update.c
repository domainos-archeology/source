/*
 * AST_$UPDATE - Periodic write-back of dirty segments and attributes
 *
 * Skipped on a diskless node.  Starting at AST_$UPDATE_SCAN, AOTEs are
 * visited in address order.  One whose attribute-flags bit 12 is set,
 * that is not in transition and has no references, has each of its
 * ASTEs that is DIRTY, unwired, not in transition and whose segment is
 * at or below AST_$UPDATE_TIMESTAMP written with ast_$update_aste (ASTE
 * in transition, AST lock released); a failure ends that AOTE's list.
 * After 32 segments in one call the timestamp is set to the last
 * segment - 1 and the pass stops with the scan left on that AOTE.  When
 * an AOTE's list is finished the timestamp is reset to 0xFFFF and the
 * AOTE purified.  The pass also stops after 0x4B AOTEs, and at the end
 * of the table it flushes the volume buffers (DBUF_$UPDATE_VOL) and
 * wraps the scan to the first AOTE.
 *
 * Original address: 0x00E016D0 (418 bytes), A5 = 0xE1DC80 (AST_ block):
 *   (0x3F4,A5) AST_$AOTE_LIMIT   (0x428,A5) AST_$AST_IN_TRANS_EC
 *   (0x484,A5) AST_$UPDATE_SCAN  (0x488,A5) AST_$UPDATE_TIMESTAMP
 * (-0x4,A6) is the status cell.  D2 = AOTEs visited, D3 = ASTEs written.
 */

#include "ast/ast_internal.h"
#include "dbuf/dbuf.h"

/* The first AOTE, `AOT` in the SAU2 map (`movea.l #0xec7b60,A2`). */
/* TODO(source-gmxj): the AST_ segment and the AST/AOT tables are still absolute on the target (tools/check_guards.py exemption). */
#if defined(ARCH_M68K)
#define AOTE_ARRAY_START ((aote_t *)0xEC7B60)
#else
#define AOTE_ARRAY_START aote_array_start
#endif

void AST_$UPDATE(void)
{
    aote_t *aote;               /* A2 */
    aste_t *aste;               /* A4 */
    uint16_t aote_count;        /* D2w */
    uint16_t aste_count;        /* D3w */
    status_$t status;           /* (-0x4,A6) */
    uint32_t *row;

    /* 0x00E016DE */
    if (NETWORK_$REALLY_DISKLESS < 0) {
        return;
    }

    /* 0x00E016E8..0x00E016FC */
    ML_$LOCK(AST_LOCK_ID);
    aote_count = 0;
    aste_count = 0;
    aote = AST_$UPDATE_SCAN;

    for (;;) {
        /* 0x00E01704..0x00E0171C: attr_flags_hi bit 4, not in transition,
         * unreferenced */
        if ((aote->attr_flags_hi & 0x10) && (int8_t)aote->flags >= 0 &&
            aote->ref_count == 0) {
            /* 0x00E01720..0x00E017C6 */
            for (aste = aote->aste_list; aste != NULL; aste = aste->next) {
                if ((int16_t)aste->flags < 0) {
                    continue;
                }
                if ((aste->flags & ASTE_FLAG_DIRTY) == 0) {
                    continue;
                }
                if (aste->wire_count != 0) {
                    continue;
                }
                if (aste->segment > AST_$UPDATE_TIMESTAMP) {   /* bhi */
                    continue;
                }

                /* 0x00E0174C..0x00E0179C: `clr.w -(SP)` is the byte flag */
                aste->flags |= ASTE_FLAG_IN_TRANS;
                ML_$UNLOCK(AST_LOCK_ID);
                row = (uint32_t *)((char *)SEGMAP_BASE +
                                   ((uint32_t)aste->seg_index << 7) - 0x80);
                ast_$update_aste(aste, (segmap_entry_t *)row, 0, &status);
                ML_$LOCK(AST_LOCK_ID);
                aste->flags &= (uint16_t)~ASTE_FLAG_IN_TRANS;
                EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);

                /* 0x00E0179E: a failure ends this AOTE's list */
                if (status != status_$ok) {
                    break;
                }

                /* 0x00E017A4..0x00E017BC: 32 written -> remember where */
                aste_count++;
                if (aste_count >= 0x20 && aste->segment != 0) {
                    AST_$UPDATE_TIMESTAMP = (uint16_t)(aste->segment - 1);
                    goto done;                          /* 0x00E01858 */
                }
            }

            /* 0x00E017CA..0x00E01800 */
            AST_$UPDATE_TIMESTAMP = 0xFFFF;
            if ((int8_t)aote->flags >= 0) {
                aote->flags |= AOTE_FLAG_IN_TRANS;
                ast_$purify_aote(aote, 0, &status);
                aote->flags &= (uint8_t)~AOTE_FLAG_IN_TRANS;
                EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);
            }
            aote_count++;
        }

        /* 0x00E01802..0x00E0180A: lea (0xc0,A2),A2; cmpa.l limit / bcs */
        aote = aote + 1;
        if (aote >= AST_$AOTE_LIMIT) {
            /* 0x00E0180C..0x00E01848 */
            if (NETWORK_$REALLY_DISKLESS >= 0) {
                ML_$UNLOCK(AST_LOCK_ID);
                DBUF_$UPDATE_VOL(0, &UID_$NIL);
                ML_$LOCK(AST_LOCK_ID);
            }
            aote = AOTE_ARRAY_START;
            break;
        }

        /* 0x00E0184A..0x00E01854 */
        if (aote_count >= 0x4B || aste_count >= 0x20) {
            break;
        }
    }

done:
    /* 0x00E01858..0x00E01862 */
    AST_$UPDATE_SCAN = aote;
    ML_$UNLOCK(AST_LOCK_ID);
}

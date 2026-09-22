/*
 * AST_$ASSOC - Associate a physical page with a page of an object
 *
 * Activates and wires the segment, then hands the page to AST_$PMAP_ASSOC
 * under the PMAP lock.  When PMAP_ASSOC reports "bad assoc" the page is
 * first faulted in with AST_$TOUCH (one page, flags | 0x42) and the
 * association retried, for as long as the touch succeeds or reports
 * "page null".  A remote object may not be associated by a type-8
 * process, and the object's concurrency word must equal `mode` or be 1.
 *
 * Parameters (frame at 0x00E0444E, `link.w A6,-0x88`):
 *   uid     (0x08,A6)  object UID
 *   seg     (0x0C,A6)  segment number, word
 *   mode    (0x0E,A6)  concurrency token, longword (D3)
 *   page    (0x12,A6)  page within the segment, word
 *   flags   (0x14,A6)  AST_$TOUCH flags, word (D4)
 *   ppn     (0x16,A6)  physical page number, longword
 *   status  (0x1A,A6)  status return (A3)
 * (-0x80,A6) is the 32-longword ppn array AST_$TOUCH fills.
 *
 * Original address: 0x00E0444E (244 bytes), A5 = 0xE1DC80 (unused here).
 */

#include "ast/ast_internal.h"

void AST_$ASSOC(uid_t *uid, uint16_t seg, uint32_t mode, uint16_t page,
                uint16_t flags, uint32_t ppn, status_$t *status)
{
    aste_t *aste;               /* A2 */
    aote_t *aote;               /* A0 */
    uint32_t concurrency;       /* D0 */
    uint16_t touch_flags;       /* D2w */
    uint32_t ppn_array[32];     /* (-0x80,A6) */

    /* 0x00E04468..0x00E04482 */
    aste = AST_$ACTIVATE_AND_WIRE(uid, seg, status);
    if (aste == NULL) {
        return;                                 /* beq.w 0x00E04538 */
    }

    /* 0x00E04486..0x00E04492: ML_$LOCK(0x14) */
    ML_$LOCK(PMAP_LOCK_ID);

    /* 0x00E04494..0x00E044BA: remote object, type-8 process -> not found */
    aote = aste->aote;
    if (aote->remote_flag < 0) {
        /*
         * cmpi.w #0x8,(-0x2,A1,D0w) with A1 = 0xE2612C and D0w = pid*2:
         * PROC1_$TYPE is declared at 0xE2612A, so this is entry [pid].
         */
        if (PROC1_$TYPE[PROC1_$CURRENT] == 8) {
            *status = file_$object_not_found;   /* 0xF0001 */
            goto unlock;
        }
    }

    /* 0x00E044BC: bset.b #0x6,(0xbf,A0) */
    aote->flags |= AOTE_FLAG_BUSY;

    /* 0x00E044C2..0x00E044D0: concurrency check on aote+0x50 */
    concurrency = aote->blocks;
    if (concurrency == mode || concurrency == 1) {
        /* 0x00E044D2: moveq #0x42 / or.w D4w,D2w */
        touch_flags = (uint16_t)(0x42 | flags);

        for (;;) {
            /* 0x00E044D6..0x00E044EA: `clr.l -(SP)` is both flag words */
            AST_$PMAP_ASSOC(aste, page, ppn, 0, 0, status);

            /* 0x00E044EE: cmpi.l #0x50006,(A3) */
            if (*status != status_$pmap_bad_assoc) {
                break;
            }

            /* 0x00E044F6..0x00E04510: fault the page in, one page */
            AST_$TOUCH(aste, mode, page, 1, ppn_array, status, touch_flags);

            /* 0x00E04514..0x00E04520: retry on ok or "page null" */
            if (*status == status_$ok || *status == status_$pmap_page_null) {
                continue;
            }
            break;
        }
    } else {
        /* 0x00E04522 */
        *status = status_$ast_write_concurrency_violation;   /* 0x30005 */
    }

unlock:
    /* 0x00E04528..0x00E04534: ML_$UNLOCK(0x14); subq.b #1,(0x11,A2) */
    ML_$UNLOCK(PMAP_LOCK_ID);
    aste->wire_count--;
}

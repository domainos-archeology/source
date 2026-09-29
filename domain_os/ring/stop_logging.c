/*
 * RINGLOG_$STOP_LOGGING - Stop ring logging and unwire the buffer
 *
 * Original address: 0x00E721CC
 * Original size: 90 bytes
 *
 * A nested procedure of RINGLOG_$CNTL.  The SAU2 link map lists the RINGLOG_
 * code segment as "I E721CC RINGLOG_ size = 168" with RINGLOG_$CNTL
 * (0xE72226) as its only exported symbol, so 0xE721CC has no name of its own;
 * both of its callers are RINGLOG_$CNTL (0x00E7228A and 0x00E72304).
 *
 * It keeps its loop index in the PARENT's frame rather than its own:
 *
 *   00e721da  movea.l (A6),A2          ; A2 = the caller's frame pointer
 *   00e721f0  move.w #0x1,(-0x2,A2)    ; parent local := 1
 *   00e721f8  move.w (-0x2,A2),D0w
 *   00e7220a  addq.w #0x1,(-0x2,A2)
 *
 * and RINGLOG_$CNTL's own frame word at (-0x2,A6) is that same cell - it is
 * the counter CNTL uses for its buffer clear at 0x00E72296-0x00E722AC, after
 * the call has returned.  The uplevel reference is flattened here into an
 * explicit parameter, as the project's nested-procedure rule requires.
 */

#include "ring/ring_internal.h"
#include "ring/ringlog_internal.h"

/*
 * RINGLOG_$STOP_LOGGING
 *
 * Parameters:
 *   parent_index - the caller's frame word at (-0x2,A6), used as the unwire
 *                  loop index and left holding wire_count + 1 on the paths
 *                  that unwire anything
 */
void RINGLOG_$STOP_LOGGING(int16_t *parent_index)
{
    int16_t remaining;

    /* 0x00E721DC tst.b (0x38,A0) / bpl - nothing to do unless logging is on */
    if (RINGLOG_$CTL.logging_active < 0) {
        /* 0x00E721E2 clr.b (0x38,A0) */
        RINGLOG_$CTL.logging_active = 0;

        /*
         * 0x00E721E6-0x00E721EC: D0 = wire_count - 1, and a negative count
         * skips the loop entirely.  D0 is then copied to D2 as the dbf
         * counter (0x00E721EE), so the body runs wire_count times.
         */
        remaining = (int16_t)(RINGLOG_$CTL.wire_count - 1);
        if (remaining >= 0) {
            /* 0x00E721F0: the parent's word is the loop index, starting at 1 */
            *parent_index = 1;
            do {
                /*
                 * 0x00E721F8-0x00E72208:
                 *   move.w (-0x2,A2),D0w / lsl.w #0x2,D0w
                 *   move.l (-0x4,A3,D0w*0x1),-(SP)   ; A3 = 0xE2C32C
                 * The -4 displacement is the Pascal [1..10] table's bias:
                 * with the index running 1..wire_count this is
                 * RINGLOG_WIRED_PAGE(index), the same entries MST_$WIRE_AREA
                 * fills from the array base (RINGLOG_$CNTL "pea (A1)",
                 * 0x00E722C6).  The page address is pushed BY VALUE.
                 */
                WP_$UNWIRE(RINGLOG_WIRED_PAGE(*parent_index));

                /* 0x00E7220A addq.w #0x1,(-0x2,A2) */
                *parent_index = (int16_t)(*parent_index + 1);

                /* 0x00E7220E dbf D2w */
                remaining = (int16_t)(remaining - 1);
            } while (remaining != -1);
        }

        /* 0x00E72212-0x00E72218 clr.w (0x30,A0) */
        RINGLOG_$CTL.wire_count = 0;
    }
}

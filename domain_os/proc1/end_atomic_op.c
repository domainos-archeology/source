/*
 * PROC1_$END_ATOMIC_OP - Leave an atomic-operation region
 * Original address: 0x00e209fa (30 bytes)
 *
 * 0x00E209FA  subq.w #0x1,(0x00e2060e).l     PROC1_$ATOMIC_OP_DEPTH--
 * 0x00E20A00  bvc.b 0x00E20A16               no signed overflow: done
 * 0x00E20A02  ori #0x700,SR                  raise, no SR saved
 * 0x00E20A06  move.w #0x0,(0x00e2060e).l     depth = 0
 * 0x00E20A0E  bsr.w PROC1_$DISPATCH_INT      (0x00E20A20)
 * 0x00E20A12  andi #-0x701,SR                forced IPL 0
 * 0x00E20A16  rts
 *
 * The overflow arm fires only when the word decrement overflows as a signed
 * quantity, i.e. when the depth was 0x8000 before the decrement (bvc tests
 * the V flag of `subq.w', not a zero crossing).  In that case the depth is
 * forced back to zero and a dispatch is run at IPL 7, leaving at IPL 0.
 *
 * Only caller: the tail of PROC1_$GET_INFO_INT (`bra.w' at 0x00E20F66,
 * proc1/sau2/misc.s).
 */

#include "proc1/proc1_internal.h"

void PROC1_$END_ATOMIC_OP(void)
{
    uint16_t before = PROC1_$ATOMIC_OP_DEPTH;

    /* 0x00E209FA */
    PROC1_$ATOMIC_OP_DEPTH = (uint16_t)(before - 1);

    /* 0x00E20A00: bvc - V is set by subq.w only for 0x8000 - 1 */
    if (before == 0x8000) {
        /* 0x00E20A02: ori #0x700,SR */
        SET_IPL7();

        /* 0x00E20A06 */
        PROC1_$ATOMIC_OP_DEPTH = 0;

        /* 0x00E20A0E */
        PROC1_$DISPATCH_INT();

        /* 0x00E20A12: andi #-0x701,SR */
        SET_IPL0();
    }
}

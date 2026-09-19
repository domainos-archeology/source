/*
 * TIME_$GET_ADJUST - Read the pending clock adjustment
 *
 * Splits TIME_$CURRENT_DELTA (0xE2B0FC, in 4-microsecond ticks) into a
 * {seconds, microseconds} pair the way TIME_$ADJUST_TIME_OF_DAY does for the
 * old delta it returns.
 *
 * Parameters:
 *   delta - receives delta[0] = seconds, delta[1] = microseconds
 *
 * Original address: 0x00e16aa8, 64 bytes
 *
 *   00e16aac  movea.l (0x8,A6),A1
 *   00e16ab0  ori #0x700,SR                 ; raise to IPL 7, nothing saved
 *   00e16ab4  move.l (0x00e2b0fc).l,D1      ; TIME_$CURRENT_DELTA
 *   00e16aba  andi #-0x701,SR               ; FORCE IPL 0 (not a restore)
 *   00e16abe  move.l #0x3d090,-(SP)         ; 250000 ticks per second
 *   00e16ac4  move.l D1,-(SP)
 *   00e16ac6  jsr M$DIS$LLL                 ; signed quotient
 *   00e16ace  move.l D0,(A1)
 *   00e16ad0  move.l #0x3d090,-(SP)
 *   00e16ad6  move.l D1,-(SP)
 *   00e16ad8  jsr M$OIS$LLL                 ; signed remainder
 *   00e16ade  lsl.l #0x2,D0                 ; ticks * 4 = microseconds
 *   00e16ae0  move.l D0,(0x4,A1)
 *
 * The second call's 8 argument bytes are never popped; unlk discards them.
 */

#include "time/time_internal.h"
#include "math/math.h"

void TIME_$GET_ADJUST(int32_t *delta)
{
    int32_t current_delta;      /* D1 */

    /* 0x00E16AB0..0x00E16ABA: ori #0x700,SR / read / andi #0xf8ff,SR */
    SET_IPL7();
    current_delta = (int32_t)TIME_$CURRENT_DELTA;
    SET_IPL0();

    /* 0x00E16ABE..0x00E16ACE */
    delta[0] = (int32_t)M$DIS$LLL(current_delta, TICKS_PER_SECOND);

    /* 0x00E16AD0..0x00E16AE0: remainder, then lsl.l #2 */
    delta[1] = (int32_t)((uint32_t)M$OIS$LLL(current_delta, TICKS_PER_SECOND) << 2);
}

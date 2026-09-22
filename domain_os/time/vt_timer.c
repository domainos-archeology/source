/*
 * TIME_$VT_TIMER - Read the virtual timer, or 0 if its interrupt is pending
 *
 * Hand-written assembly in the TIME_ASM segment.  It takes no arguments
 * and returns its result in D0w, which is the C calling convention, so it
 * is emitted in C like the clock readers (bead source-6b8c); its callers
 * (PROC1's dispatcher and PROC1_$VT_INT) are assembly.
 *
 * Original address: 0x00e2af6c, 30 bytes
 *
 *   00e2af6c  lea (0xffac00).l,A0
 *   00e2af72  movep.w (0x9,A0),D0w            ; bytes 0xFFAC09 / 0xFFAC0B
 *   00e2af76  btst.b #0x1,(0x3,A0) / bne -> 0  ; VT interrupt pending
 *   00e2af7e  tst.b (0x00e2af6a).l / beq -> rts ; IN_VT_INT set -> 0
 *   00e2af86  clr.w D0w
 *   00e2af88  rts
 *
 * The counter is read BEFORE the two tests; only the returned value is
 * replaced by zero.  D0's upper word is whatever the caller left there.
 */

#include "time/time_internal.h"

/* `movep.w (0x9,A0),D0w`: high byte from +9, low byte from +0xB */
#define TIME_$READ_VT_TIMER() \
    ((uint16_t)(((uint16_t)TIME_$TIMER_READ(TIME_TIMER_VT_HI) << 8) | \
                (uint16_t)TIME_$TIMER_READ(TIME_TIMER_VT_LO)))

uint16_t TIME_$VT_TIMER(void)
{
    uint16_t value;         /* D0w */

    /* 0x00E2AF72 */
    value = TIME_$READ_VT_TIMER();

    /* 0x00E2AF76..0x00E2AF84 */
    if ((TIME_$TIMER_READ(TIME_TIMER_CTRL) & TIME_CTRL_VT_INT) != 0 ||
        IN_VT_INT != 0) {
        /* 0x00E2AF86 */
        value = 0;
    }

    return value;
}

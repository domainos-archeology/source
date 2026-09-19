/*
 * TIME_$GET_TIME_OF_DAY - Get the current time of day
 *
 * Same hardware read as TIME_$CLOCK (0x00E2AFD6): the elapsed ticks since
 * the last timer interrupt are recovered from the down-counter, folded in
 * if an interrupt is already pending, clamped to TIME_$CURRENT_TICK, then
 * converted to microseconds (4 us per tick) and added to the stored
 * {TIME_$CURRENT_TIME, TIME_$CURRENT_USEC} pair with a one-second carry.
 *
 * Hand-written assembly in the TIME_ASM segment (no link frame, argument
 * read with `movea.l (0x4,SP),A0`, SR save/restore in D1); emitted in C for
 * the same reason as time/clock.c.
 *
 * Parameters:
 *   tv - receives tv[0] = seconds, tv[1] = microseconds
 *
 * Original address: 0x00e2b06a, 94 bytes
 *
 * PC-relative data cells (extension word address + displacement):
 *   00e2b076  movea.l (0x78,PC),A1    0xE2B078 + 0x78 = 0xE2B0F0 TIME_$CURRENT_TIME
 *   00e2b094  add.w   (0x62,PC),D0w   0xE2B096 + 0x62 = 0xE2B0F8 TIME_$CURRENT_TICK
 *   00e2b09c  cmp.w   (0x5a,PC),D0w   0xE2B09E + 0x5A = 0xE2B0F8 TIME_$CURRENT_TICK
 *   00e2b0a2  move.w  (0x54,PC),D0w   0xE2B0A4 + 0x54 = 0xE2B0F8 TIME_$CURRENT_TICK
 *   00e2b0a8  add.l   (0x4a,PC),D0    0xE2B0AA + 0x4A = 0xE2B0F4 TIME_$CURRENT_USEC
 */

#include "time/time_internal.h"

void TIME_$GET_TIME_OF_DAY(uint32_t *tv)
{
    uint16_t saved_sr;      /* D1 */
    uint32_t seconds;       /* A1 */
    uint16_t ticks;         /* D0w */
    uint16_t sum;
    uint32_t usecs;         /* D0 after lsl.l */

    /* 0x00E2B070..0x00E2B074: move SR,D1w / ori #0x700,SR */
    DISABLE_INTERRUPTS(saved_sr);

    /* 0x00E2B076: movea.l (0x78,PC),A1 = TIME_$CURRENT_TIME */
    seconds = TIME_$CURRENT_TIME;

    /* 0x00E2B07A..0x00E2B082: clr.l D0 / movep.w / not.w / add.w #0x1047 */
    ticks = (uint16_t)(~TIME_$READ_RTE_TIMER() + TIME_INITIAL_TICK);

    /* 0x00E2B086: cmp.w #0xfe3,D0w / bgt.b 0x00e2b09c (SIGNED) */
    if ((int16_t)ticks <= 0x0FE3) {
        /* 0x00E2B08C: btst.b #0x0,(0x3,A0) - RTE interrupt pending */
        if ((TIME_$TIMER_READ(TIME_TIMER_CTRL) & TIME_CTRL_RTE_INT) != 0) {
            /*
             * 0x00E2B094..0x00E2B09A: add.w (0x62,PC),D0w / bcc.b /
             * addq.l #0x1,A1 - a carry out of the tick word bumps the
             * SECONDS count (A1 holds TIME_$CURRENT_TIME here).
             */
            sum = (uint16_t)(ticks + TIME_$CURRENT_TICK);
            if (sum < ticks) {
                seconds++;
            }
            ticks = sum;
        }
    }

    /* 0x00E2B09C..0x00E2B0A2: SIGNED clamp to TIME_$CURRENT_TICK */
    if ((int16_t)ticks > (int16_t)TIME_$CURRENT_TICK) {
        ticks = TIME_$CURRENT_TICK;
    }

    /*
     * 0x00E2B0A6..0x00E2B0A8: lsl.l #0x2,D0 / add.l (0x4a,PC),D0.  D0 was
     * cleared before the movep, and every later operation on it was a word
     * op, so the longword is the zero-extended tick count times 4.
     */
    usecs = ((uint32_t)ticks << 2) + TIME_$CURRENT_USEC;

    /*
     * 0x00E2B0AC..0x00E2B0B6: cmp.l #0xf4240,D0 / bmi.b - the branch is on
     * the N flag of (D0 - 1000000), i.e. a SIGNED "less than"; otherwise
     * addq.l #0x1,A1 / sub.l #0xf4240,D0.
     */
    if (!((int32_t)(usecs - 1000000u) < 0)) {
        seconds++;
        usecs -= 1000000u;
    }

    /* 0x00E2B0BC: move D1w,SR */
    ENABLE_INTERRUPTS(saved_sr);

    /* 0x00E2B0BE..0x00E2B0C4: movea.l (0x4,SP),A0 / move.l A1,(A0)+ / move.l D0,(A0) */
    tv[0] = seconds;
    tv[1] = usecs;
}

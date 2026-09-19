/*
 * TIME_$CLOCK - Get the current (raw) 48-bit clock
 *
 * Reads the real-time hardware timer, converts it to "ticks elapsed since
 * the last timer interrupt" and adds those ticks to the stored clock
 * TIME_$CURRENT_CLOCKH/L (0xE2B0E4 / 0xE2B0E8).
 *
 * This is hand-written assembly in the TIME_ASM segment (map: E2ADFC
 * TIME_ASM, size 484): no link frame, the argument is read straight off the
 * stack (`movea.l (0x4,SP),A0`) and the SR is bracketed with a true
 * save/restore (`move SR,D1w` / `move D1w,SR`).  It is emitted in C, like
 * the neighbouring TIME_$READ_CAL, so the host tests can drive it through
 * the TIME_$TIMER_READ abstraction in time/time.h.
 *
 * Parameters:
 *   clock - receives the 48-bit clock value
 *
 * Original address: 0x00e2afd6, 80 bytes
 *
 * PC-relative data cells (extension word address + displacement):
 *   00e2afe2  movea.l (0x100,PC),A1   0xE2AFE4 + 0x100 = 0xE2B0E4 TIME_$CURRENT_CLOCKH
 *   00e2b000  add.w   (0xf6,PC),D0w   0xE2B002 + 0xF6  = 0xE2B0F8 TIME_$CURRENT_TICK
 *   00e2b008  cmp.w   (0xee,PC),D0w   0xE2B00A + 0xEE  = 0xE2B0F8 TIME_$CURRENT_TICK
 *   00e2b00e  move.w  (0xe8,PC),D0w   0xE2B010 + 0xE8  = 0xE2B0F8 TIME_$CURRENT_TICK
 *   00e2b012  add.w   (0xd4,PC),D0w   0xE2B014 + 0xD4  = 0xE2B0E8 TIME_$CURRENT_CLOCKL
 *
 * The block from 0x00E2B008 to the `rts` at 0x00E2B024 is shared: TIME_$ABS_CLOCK
 * (0x00E2B026) branches into it with `bgt.b 0x00e2b008` when the elapsed
 * tick count is above 0xFE3.  time/abs_clock.c reproduces that tail inline.
 */

#include "time/time_internal.h"

void TIME_$CLOCK(clock_t *clock)
{
    uint16_t saved_sr;      /* D1 */
    uint32_t high;          /* A1 */
    uint16_t ticks;         /* D0w */
    uint16_t sum;

    /* 0x00E2AFDC..0x00E2AFE0: move SR,D1w / ori #0x700,SR */
    DISABLE_INTERRUPTS(saved_sr);

    /* 0x00E2AFE2: movea.l (0x100,PC),A1 = TIME_$CURRENT_CLOCKH */
    high = TIME_$CURRENT_CLOCKH;

    /*
     * 0x00E2AFE6..0x00E2AFEE: clr.l D0 / movep.w (0x5,A0),D0w / not.w D0w /
     * add.w #0x1047,D0w.  The timer counts DOWN from 0x1047, so the
     * complement plus 0x1047 is the number of ticks elapsed since it was
     * reloaded (the `clr.l D0` only matters for the unused high half).
     */
    ticks = (uint16_t)(~TIME_$READ_RTE_TIMER() + TIME_INITIAL_TICK);

    /*
     * 0x00E2AFF2..0x00E2B006: cmp.w #0xfe3,D0w / bgt.b 0x00e2b008.  The
     * compare is SIGNED: a value at or below 0x0FE3 (including anything with
     * bit 15 set) goes on to look at the interrupt-pending bit.
     */
    if ((int16_t)ticks <= 0x0FE3) {
        /* 0x00E2AFF8: btst.b #0x0,(0x3,A0) - RTE interrupt pending */
        if ((TIME_$TIMER_READ(TIME_TIMER_CTRL) & TIME_CTRL_RTE_INT) != 0) {
            /*
             * 0x00E2B000..0x00E2B006: the timer has already wrapped but the
             * interrupt that would fold its period into the stored clock has
             * not run yet, so add a whole period (TIME_$CURRENT_TICK) here;
             * `bcc` / `addq.l #0x1,A1` propagates the 16-bit carry.
             */
            sum = (uint16_t)(ticks + TIME_$CURRENT_TICK);
            if (sum < ticks) {
                high++;
            }
            ticks = sum;
        }
    }

    /*
     * 0x00E2B008..0x00E2B010: cmp.w (0xee,PC),D0w / ble.b / move.w
     * (0xe8,PC),D0w - a SIGNED clamp of the tick count to TIME_$CURRENT_TICK.
     */
    if ((int16_t)ticks > (int16_t)TIME_$CURRENT_TICK) {
        ticks = TIME_$CURRENT_TICK;
    }

    /* 0x00E2B012..0x00E2B018: add.w (0xd4,PC),D0w / bcc.b / addq.l #0x1,A1 */
    sum = (uint16_t)(ticks + TIME_$CURRENT_CLOCKL);
    if (sum < ticks) {
        high++;
    }
    ticks = sum;

    /* 0x00E2B01A: move D1w,SR */
    ENABLE_INTERRUPTS(saved_sr);

    /* 0x00E2B01C..0x00E2B022: movea.l (0x4,SP),A0 / move.l A1,(A0)+ / move.w D0w,(A0) */
    clock->high = high;
    clock->low = ticks;
}

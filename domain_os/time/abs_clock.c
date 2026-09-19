/*
 * TIME_$ABS_CLOCK - Get the absolute (adjusted) 48-bit clock
 *
 * Same shape as TIME_$CLOCK (0x00E2AFD6) but starts from the adjusted clock
 * TIME_$CLOCKH / TIME_$CLOCKL (0xE2B0D4 / 0xE2B0E0) and, on the
 * interrupt-pending path, folds in a constant period of 0x1047 rather than
 * TIME_$CURRENT_TICK.
 *
 * Hand-written assembly in the TIME_ASM segment (no link frame, argument
 * read with `movea.l (0x4,SP),A0`, SR save/restore in D1); emitted in C for
 * the same reason as time/clock.c.
 *
 * Parameters:
 *   clock - receives the 48-bit clock value
 *
 * Original address: 0x00e2b026, 68 bytes
 *
 * PC-relative data cells (extension word address + displacement):
 *   00e2b032  movea.l (0xa0,PC),A1    0xE2B034 + 0xA0 = 0xE2B0D4 TIME_$CLOCKH
 *   00e2b056  add.w   (0x88,PC),D0w   0xE2B058 + 0x88 = 0xE2B0E0 TIME_$CLOCKL
 *
 * QUIRK reproduced below: `bgt.b 0x00e2b008` at 0x00E2B044 does not branch
 * within this routine - it jumps into the tail of TIME_$CLOCK.  On that path
 * (elapsed ticks above 0xFE3, signed) the value is clamped to
 * TIME_$CURRENT_TICK and TIME_$CURRENT_CLOCKL (0xE2B0E8) is added to the
 * low word, while the high longword in A1 is still TIME_$CLOCKH.  Only the
 * fall-through path (ticks <= 0xFE3) adds TIME_$CLOCKL.
 */

#include "time/time_internal.h"

void TIME_$ABS_CLOCK(clock_t *clock)
{
    uint16_t saved_sr;      /* D1 */
    uint32_t high;          /* A1 */
    uint16_t ticks;         /* D0w */
    uint16_t sum;

    /* 0x00E2B02C..0x00E2B030: move SR,D1w / ori #0x700,SR */
    DISABLE_INTERRUPTS(saved_sr);

    /* 0x00E2B032: movea.l (0xa0,PC),A1 = TIME_$CLOCKH */
    high = TIME_$CLOCKH;

    /* 0x00E2B036..0x00E2B03C: movep.w (0x5,A0),D0w / not.w D0w / add.w #0x1047,D0w */
    ticks = (uint16_t)(~TIME_$READ_RTE_TIMER() + TIME_INITIAL_TICK);

    /* 0x00E2B040: cmp.w #0xfe3,D0w / bgt.b 0x00e2b008 (SIGNED compare) */
    if ((int16_t)ticks > 0x0FE3) {
        /*
         * TIME_$CLOCK's tail, 0x00E2B008..0x00E2B018: cmp.w (0xee,PC),D0w /
         * ble.b / move.w (0xe8,PC),D0w clamps to TIME_$CURRENT_TICK, then
         * add.w (0xd4,PC),D0w / bcc.b / addq.l #0x1,A1 adds
         * TIME_$CURRENT_CLOCKL - not TIME_$CLOCKL - with carry into A1.
         */
        if ((int16_t)ticks > (int16_t)TIME_$CURRENT_TICK) {
            ticks = TIME_$CURRENT_TICK;
        }
        sum = (uint16_t)(ticks + TIME_$CURRENT_CLOCKL);
        if (sum < ticks) {
            high++;
        }
        ticks = sum;
    } else {
        /* 0x00E2B046: btst.b #0x0,(0x3,A0) - RTE interrupt pending */
        if ((TIME_$TIMER_READ(TIME_TIMER_CTRL) & TIME_CTRL_RTE_INT) != 0) {
            /* 0x00E2B04E..0x00E2B054: add.w #0x1047,D0w / bcc.b / addq.l #0x1,A1 */
            sum = (uint16_t)(ticks + TIME_INITIAL_TICK);
            if (sum < ticks) {
                high++;
            }
            ticks = sum;
        }

        /* 0x00E2B056..0x00E2B05C: add.w (0x88,PC),D0w / bcc.b / addq.l #0x1,A1 */
        sum = (uint16_t)(ticks + TIME_$CLOCKL);
        if (sum < ticks) {
            high++;
        }
        ticks = sum;
    }

    /* 0x00E2B05E (or 0x00E2B01A on the shared tail): move D1w,SR */
    ENABLE_INTERRUPTS(saved_sr);

    /* 0x00E2B060..0x00E2B066: movea.l (0x4,SP),A0 / move.l A1,(A0)+ / move.w D0w,(A0) */
    clock->high = high;
    clock->low = ticks;
}

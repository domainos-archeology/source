/*
 * TIME_$WRT_TIMER - Load one of the four hardware timer counters
 *
 * Writes a 16-bit value through `movep.w` into timer *timer_index (0..3):
 * the odd bytes at 0xFFAC00 + index*4 + 1 and + 3.  Loading timer 2 (the
 * virtual timer) clears IN_VT_INT; loading anything above 2 (timer 3)
 * clears IN_RT_INT; timers 0 and 1 clear nothing.
 *
 * Hand-written assembly in the TIME_ASM segment but with C-style stack
 * arguments (`movea.l (0x4,SP),A0` / `(0x8,SP)`), so it is emitted in C.
 *
 * Parameters:
 *   timer_index - by reference, word: 0 control, 1 real-time, 2 virtual, 3 aux
 *   value       - by reference, word
 *
 * Original address: 0x00e2afa0, 54 bytes
 *
 *   00e2afa0  movea.l (0x4,SP),A0 / clr.l D0 / move.w (A0),D0w
 *   00e2afa8  movea.l (0x8,SP),A0 / move.w (A0),D1w
 *   00e2afae  lea (0xffac00).l,A0
 *   00e2afb4  asl.w #0x2,D0w                  ; index * 4, a 16-bit result
 *   00e2afb6  lea (0x0,A0,D0w*0x1),A0         ; sign-extended word index
 *   00e2afba  movep.w D1w,(0x1,A0)            ; high byte -> +1, low -> +3
 *   00e2afbe  cmp.w #0x8,D0w / beq -> clear IN_VT_INT
 *   00e2afc4  blt -> rts
 *   00e2afc6  sf (0x00e2af6b).l               ; IN_RT_INT = 0
 *   00e2afce  sf (0x00e2af6a).l               ; IN_VT_INT = 0
 */

#include "time/time_internal.h"

void TIME_$WRT_TIMER(uint16_t *timer_index, uint16_t *value)
{
    int16_t offset;         /* D0w after asl.w */
    uint16_t val;           /* D1w */

    /* 0x00E2AFA0..0x00E2AFB4 */
    offset = (int16_t)(*timer_index << 2);
    val = *value;

    /* 0x00E2AFB6..0x00E2AFBA */
    TIME_$TIMER_WRITE(offset + 1, (uint8_t)(val >> 8));
    TIME_$TIMER_WRITE(offset + 3, (uint8_t)(val & 0xFF));

    /* 0x00E2AFBE..0x00E2AFCE: signed compare of the byte offset with 8 */
    if (offset == 8) {
        IN_VT_INT = 0;
    } else if (offset > 8) {
        IN_RT_INT = 0;
    }
}

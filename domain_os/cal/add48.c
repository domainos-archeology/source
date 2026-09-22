/*
 * ADD48 - 48-bit add: *dst += *src
 *
 * Hand-written assembly in the TIME_ASM code segment (map: I E172D0
 * TIME_ASM size 2C holds ADD48 and SUB48): no link frame, both arguments
 * read straight off the stack, and the carry threaded through X from the
 * low word into the high longword.  Kept in C, like the other TIME_ASM
 * routines, so the many host tests that exercise clock arithmetic can
 * include it (bead source-6b8c).
 *
 * Original address: 0x00e172d0, 20 bytes
 *
 *   00e172d0  movea.l (0x4,SP),A0      ; dst
 *   00e172d4  movea.l (0x8,SP),A1      ; src
 *   00e172d8  addq.l #0x4,A0           ; -> the low words
 *   00e172da  addq.l #0x4,A1
 *   00e172dc  move.w (A1),D0w
 *   00e172de  add.w D0w,(A0)           ; dst->low += src->low, sets X
 *   00e172e0  addx.l -(A1),-(A0)       ; dst->high += src->high + X
 *   00e172e2  rts
 *
 * D0 is left holding src->low (zero-extended junk in the upper half); no
 * caller reads it.
 */

#include "cal/cal_internal.h"

void ADD48(clock_t *dst, clock_t *src)
{
    uint16_t dst_low = dst->low;
    uint16_t sum_low = (uint16_t)(dst_low + src->low);

    /* 0x00E172DE: the word add's carry ... */
    dst->low = sum_low;

    /* 0x00E172E0: ... is added into the longword add */
    dst->high = dst->high + src->high + (sum_low < dst_low ? 1u : 0u);
}

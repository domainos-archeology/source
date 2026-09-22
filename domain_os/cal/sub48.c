/*
 * SUB48 - 48-bit subtract: *dst -= *src; returns "result is non-negative"
 *
 * Hand-written assembly in the TIME_ASM code segment next to ADD48 (see
 * cal/add48.c for why it stays in C).  The borrow is threaded through X
 * from the low word into the high longword, and `spl D0b` turns the N flag
 * of the high longword into a Domain boolean: 0xFF when the 48-bit result
 * is NON-negative, 0 when it went negative.
 *
 * Original address: 0x00e172e4, 22 bytes
 *
 *   00e172e4  movea.l (0x4,SP),A0      ; dst
 *   00e172e8  movea.l (0x8,SP),A1      ; src
 *   00e172ec  addq.l #0x4,A0 / addq.l #0x4,A1
 *   00e172f0  move.w (A1),D0w
 *   00e172f2  sub.w D0w,(A0)           ; dst->low -= src->low, sets X
 *   00e172f4  subx.l -(A1),-(A0)       ; dst->high -= src->high + X
 *   00e172f6  spl D0b                  ; 0xFF if N clear
 *   00e172f8  rts
 */

#include "cal/cal_internal.h"

int8_t SUB48(clock_t *dst, clock_t *src)
{
    uint16_t dst_low = dst->low;
    uint16_t src_low = src->low;

    /* 0x00E172F2 */
    dst->low = (uint16_t)(dst_low - src_low);

    /* 0x00E172F4 */
    dst->high = dst->high - src->high - (dst_low < src_low ? 1u : 0u);

    /* 0x00E172F6: spl on the high longword's sign */
    return ((int32_t)dst->high >= 0) ? (int8_t)-1 : (int8_t)0;
}

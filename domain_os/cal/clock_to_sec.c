/*
 * CAL_$CLOCK_TO_SEC - 48-bit clock (4 us ticks) -> whole seconds
 *
 * Divides the 48-bit tick count by 250000 as (ticks >> 4) / 15625, in two
 * 32/16 `divu.w` steps whose remainder carries from the first into the
 * second.
 *
 * Hand-written assembly (no link frame; D2 saved by hand, the argument read
 * at (0x8,SP)) in the CAL_ code segment; kept in C so the host tests can
 * include it (see bead source-6b8c for the TIME_ASM siblings).
 *
 * Parameters:
 *   clock - the clock_t to convert ((0x8,SP) after the D2 push)
 *
 * Returns (D0): seconds
 *
 * Original address: 0x00e817c4, 38 bytes
 *
 *   00e817c4  move.l D2,-(SP)
 *   00e817c6  movea.l (0x8,SP),A0
 *   00e817ca  move.l (A0),D0            ; high
 *   00e817cc  lsr.l #0x4,D0
 *   00e817ce  divu.w #0x3d09,D0         ; D0 = rem1:quot1
 *   00e817d2  move.w D0w,D1w            ; D1w = quot1
 *   00e817d4  move.l (0x2,A0),D2        ; the longword straddling high/low:
 *                                       ;   (high & 0xFFFF) << 16 | low
 *   00e817d8  lsr.l #0x4,D2
 *   00e817da  move.w D2w,D0w            ; D0 = rem1:(low32 >> 4).w
 *   00e817dc  divu.w #0x3d09,D0         ; D0 = rem2:quot2
 *   00e817e0  swap D0 / move.w D1w,D0w / swap D0   ; D0 = quot1:quot2
 *   00e817e6  move.l (SP)+,D2
 *   00e817e8  rts
 *
 * Neither `divu.w` can overflow: (high >> 4) / 15625 fits 16 bits for every
 * 32-bit high, and the second dividend is at most (15624 << 16) | 0xFFFF.
 */

#include "cal/cal_internal.h"

ulong CAL_$CLOCK_TO_SEC(clock_t *clock)
{
    uint32_t high_shifted;      /* D0 before the first divu */
    uint16_t quot1;             /* D1w */
    uint16_t rem1;
    uint32_t low32_shifted;     /* D2 */
    uint32_t dividend2;
    uint16_t quot2;

    /* 0x00E817CA..0x00E817D2 */
    high_shifted = clock->high >> 4;
    quot1 = (uint16_t)(high_shifted / 0x3D09u);
    rem1 = (uint16_t)(high_shifted % 0x3D09u);

    /* 0x00E817D4..0x00E817D8: the longword at clock + 2 (big-endian) */
    low32_shifted = (((clock->high & 0xFFFFu) << 16) | clock->low) >> 4;

    /* 0x00E817DA..0x00E817DC */
    dividend2 = ((uint32_t)rem1 << 16) | (low32_shifted & 0xFFFFu);
    quot2 = (uint16_t)(dividend2 / 0x3D09u);

    /* 0x00E817E0..0x00E817E4 */
    return ((ulong)quot1 << 16) | quot2;
}

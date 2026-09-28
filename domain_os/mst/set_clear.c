/*
 * MST_$SET_CLEAR - Exclude a member from a Domain Pascal set (clear a bit)
 *
 * Original address: 0x00E1A118 (SAU2 map: segment MST_ASM, size 0x26,
 * holding MST_$SET at E1A0FC and MST_$SET_CLEAR at E1A118)
 * Size: 28 bytes (0x00E1A118 .. 0x00E1A133)
 *
 * Instruction for instruction the twin of MST_$SET (see mst/set.c) with
 * `bclr.b` in place of `bset.b`: no link frame, arguments straight off the
 * stack, caller cleans up.
 *
 *   0x00E1A118  move.w (0xa,SP),D0w         bit_index   (argument 3, word)
 *   0x00E1A11C  move.w (0x8,SP),D1w         size        (argument 2, word)
 *   0x00E1A120  subq.w #0x1,D1w
 *   0x00E1A122  or.w #0xf,D1w
 *   0x00E1A126  sub.w D0w,D1w
 *   0x00E1A128  lsr.w #0x3,D1w              byte offset (unsigned shift)
 *   0x00E1A12A  movea.l (0x4,SP),A0         bitmap      (argument 1)
 *   0x00E1A12E  bclr.b D0,(0x0,A0,D1w*0x1)  bit (bit_index mod 8) of that byte
 *   0x00E1A132  rts
 *
 * Verified against the disassembly 2026-09-19; the body was already faithful.
 */

#include "mst/mst_internal.h"

/*
 * @param bitmap    The set
 * @param size      Number of members the set was declared with
 * @param bit_index Member to exclude (0-based)
 */
void MST_$SET_CLEAR(void *bitmap, uint16_t size, uint16_t bit_index)
{
    uint8_t *bytes = (uint8_t *)bitmap;
    int16_t byte_offset;

    /* 0x00E1A11C .. 0x00E1A128 */
    byte_offset = (int16_t)((uint16_t)(((uint16_t)(size - 1) | 0x0f) - bit_index) >> 3);

    /* 0x00E1A12E bclr.b D0,(0x0,A0,D1w*0x1) */
    bytes[byte_offset] &= (uint8_t)~(1u << (bit_index & 7));
}

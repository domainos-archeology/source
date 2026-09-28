/*
 * MST_$SET - Include a member in a Domain Pascal set (set a bit)
 *
 * Original address: 0x00E1A0FC (SAU2 map: segment MST_ASM, size 0x26,
 * holding MST_$SET at E1A0FC and MST_$SET_CLEAR at E1A118)
 * Size: 28 bytes (0x00E1A0FC .. 0x00E1A117)
 *
 * The segment name says this pair was written in assembly: there is no
 * link frame, the arguments are read straight off the stack and D0/D1/A0
 * are used as scratch.  The calling convention is nevertheless the normal
 * C-style one (caller pushes right to left and cleans up), so the routine
 * is kept in C.
 *
 *   0x00E1A0FC  move.w (0xa,SP),D0w         bit_index   (argument 3, word)
 *   0x00E1A100  move.w (0x8,SP),D1w         size        (argument 2, word)
 *   0x00E1A104  subq.w #0x1,D1w
 *   0x00E1A106  or.w #0xf,D1w               round (size - 1) up to ...1111
 *   0x00E1A10A  sub.w D0w,D1w
 *   0x00E1A10C  lsr.w #0x3,D1w              byte offset (unsigned shift)
 *   0x00E1A10E  movea.l (0x4,SP),A0         bitmap      (argument 1)
 *   0x00E1A112  bset.b D0,(0x0,A0,D1w*0x1)  bit (bit_index mod 8) of that byte
 *   0x00E1A116  rts
 *
 * The byte offset is applied as a sign-extended word index (`D1w*0x1`).
 * Member N of the set therefore lives in byte ((size-1)|0xf - N) >> 3 at
 * bit N & 7: the set is stored as a big-endian multi-word integer with
 * member 0 in the least significant bit of its last byte.
 *
 * Verified against the disassembly 2026-09-19; the body was already faithful.
 */

#include "mst/mst_internal.h"

/*
 * @param bitmap    The set
 * @param size      Number of members the set was declared with
 * @param bit_index Member to include (0-based)
 */
void MST_$SET(void *bitmap, uint16_t size, uint16_t bit_index)
{
    uint8_t *bytes = (uint8_t *)bitmap;
    int16_t byte_offset;

    /* 0x00E1A100 .. 0x00E1A10C */
    byte_offset = (int16_t)((uint16_t)(((uint16_t)(size - 1) | 0x0f) - bit_index) >> 3);

    /* 0x00E1A112 bset.b D0,(0x0,A0,D1w*0x1) */
    bytes[byte_offset] |= (uint8_t)(1u << (bit_index & 7));
}

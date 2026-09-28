/*
 * PAS_$SET_BUILD - build a Pascal set from a template and a range
 *
 * Original address: 0x00E11FA8
 * Size: 74 bytes (0x00E11FA8 .. 0x00E11FF1)
 *
 * Runtime support for the Pascal set constructor `[lo..hi]`: the
 * destination set is first copied from a template, then every member
 * from lo to hi (clamped to 0 .. total) is included.  Member N lives at
 * bit N & 7 of byte ((total | 0xF) - N) >> 3, the same layout MST_$SET
 * uses.
 *
 * No link frame; the arguments are read off the caller's stack after the
 * three saved registers (movem.l {A2 D3 D2}, 12 bytes):
 *   (0x10,SP)  dest   -> A1 (and A2 for the copy)
 *   (0x14,SP)  src    -> A0
 *   (0x18,SP)  lo     word -> D1w
 *   (0x1a,SP)  hi     word -> D2w
 *   (0x1c,SP)  total  word -> D0w, the declared member count
 *
 *   0x00E11FBA  move.w D0w,D1w / lsr.w #4,D1w      (total >> 4) + 1 words
 *   0x00E11FBE  move.w (A0)+,(A2)+ / dbf D1w       copied
 *   0x00E11FC4  move.w (0x18,SP),D1w / bge / clr.w  lo < 0 -> 0 (signed)
 *   0x00E11FCC  move.w (0x1a,SP),D2w / cmp.w D2w,D0w / bge / move.w D0w,D2w
 *                                                  hi > total -> total (signed)
 *   0x00E11FD6  or.w #0xf,D0w
 *   0x00E11FDA  cmp.w D1w,D2w / blt done           while lo <= hi (signed):
 *   0x00E11FDE    D3w = (D0w - D1w) >> 3 (logical)
 *   0x00E11FE4    bset.b D1,(0x0,A1,D3w*0x1)       bit lo & 7 of dest[D3w]
 *   0x00E11FE8    addq.w #1,D1w
 *
 * Verified against the disassembly 2026-09-27; the body was already
 * faithful and is restated with the register widths made explicit.
 */

#include "pas/pas_internal.h"

void PAS_$SET_BUILD(
    uint16_t *dest,
    uint16_t *src,
    int16_t start_bit,
    int16_t end_bit,
    uint16_t total_bits)
{
    uint16_t *dst = dest;                   /* A2 */
    uint8_t *bytes = (uint8_t *)dest;       /* A1 */
    uint16_t d0w = total_bits;              /* D0w */
    int16_t d1w;                            /* D1w */
    int16_t d2w;                            /* D2w */
    int16_t d3w;                            /* D3w */
    int16_t count;

    /* 0x00E11FBA .. 0x00E11FC0: (total >> 4) + 1 words */
    for (count = (int16_t)(d0w >> 4); count != -1; count--) {
        *dst++ = *src++;
    }

    /* 0x00E11FC4 .. 0x00E11FCA */
    d1w = start_bit;
    if (d1w < 0) {
        d1w = 0;
    }

    /* 0x00E11FCC .. 0x00E11FD4: signed compare of total with hi */
    d2w = end_bit;
    if ((int16_t)d0w < d2w) {
        d2w = (int16_t)d0w;
    }

    /* 0x00E11FD6 */
    d0w |= 0x000F;

    /* 0x00E11FDA .. 0x00E11FEA */
    while (d2w >= d1w) {
        d3w = (int16_t)((uint16_t)(d0w - (uint16_t)d1w) >> 3);
        bytes[d3w] |= (uint8_t)(1u << (d1w & 7));
        d1w++;
    }
}

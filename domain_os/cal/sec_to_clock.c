/*
 * CAL_$SEC_TO_CLOCK - signed seconds -> 48-bit clock (4 us ticks)
 *
 * Multiplies |sec| by 250000 = 0x3D090 = 3 * 0x10000 + 0xD090 with four
 * 16x16 `mulu.w` partial products, then negates the 48-bit result when the
 * input was negative.
 *
 * Hand-written assembly (no link frame; D2/D3 saved by hand, arguments at
 * (0xc,SP) and (0x10,SP)) in the CAL_ code segment; kept in C so the host
 * tests can include it (see bead source-6b8c for the TIME_ASM siblings).
 *
 * Parameters:
 *   sec       - the seconds, read as a SIGNED longword (bge at 0x00E81768)
 *   clock_ret - receives the clock
 *
 * Original address: 0x00e8175c, 80 bytes
 *
 *   00e81760  clr.w D3w                      ; negative flag
 *   00e81766  move.l (A0),D0 / bge / neg.l D0 / moveq #1,D3
 *   00e81772  move.w D0w,D2w / mulu.w #0xd090,D2      ; low16 * 0xD090
 *   00e81778  move.w D2w,(0x4,A0)            ; clock->low
 *   00e8177c  clr.w D2w / swap D2            ; D2 = product >> 16
 *   00e81780  move.w D0w,D1w / mulu.w #3,D1 / add.l D1,D2   ; + low16 * 3
 *   00e81788  swap D0 / move.w D0w,D1w / mulu.w #0xd090,D1 / add.l D1,D2
 *                                            ; + high16 * 0xD090
 *   00e81792  move.l D2,(A0)                 ; clock->high
 *   00e81794  mulu.w #3,D0 / add.w (A0),D0w / move.w D0w,(A0)
 *                                            ; + high16 * 3 into the TOP word
 *   00e8179c  tst.b D3b / beq
 *   00e817a0  neg.l (0x2,A0) / negx.w (A0)   ; 48-bit negate: the longword
 *                                            ; straddling high/low, then the
 *                                            ; top word with the borrow
 */

#include "cal/cal_internal.h"

void CAL_$SEC_TO_CLOCK(uint *sec, clock_t *clock_ret)
{
    uint32_t s;             /* D0 */
    int8_t negative;        /* D3b */
    uint32_t product_low;   /* D2 after the first mulu */
    uint32_t acc;           /* D2 */
    uint16_t low16, high16;
    uint16_t top_word;
    uint32_t low32;
    uint32_t borrow;

    /* 0x00E81760..0x00E8176C */
    negative = 0;
    s = *sec;
    if ((int32_t)s < 0) {
        s = (uint32_t)-(int32_t)s;
        negative = 1;
    }
    low16 = (uint16_t)s;
    high16 = (uint16_t)(s >> 16);

    /* 0x00E81772..0x00E81778 */
    product_low = (uint32_t)low16 * 0xD090u;
    clock_ret->low = (uint16_t)product_low;

    /* 0x00E8177C..0x00E81792 */
    acc = product_low >> 16;
    acc += (uint32_t)low16 * 3u;
    acc += (uint32_t)high16 * 0xD090u;
    clock_ret->high = acc;

    /* 0x00E81794..0x00E8179A: add.w into the big-endian top word of high */
    top_word = (uint16_t)((clock_ret->high >> 16) + (uint16_t)(high16 * 3u));
    clock_ret->high = ((uint32_t)top_word << 16) | (clock_ret->high & 0xFFFFu);

    /* 0x00E8179C..0x00E817A4 */
    if (negative != 0) {
        /* neg.l (0x2,A0): the 32 bits made of high's low word and low */
        low32 = ((clock_ret->high & 0xFFFFu) << 16) | clock_ret->low;
        borrow = (low32 != 0) ? 1u : 0u;        /* X after the neg.l */
        low32 = (uint32_t)-(int32_t)low32;
        /* negx.w (A0): 0 - top_word - X */
        top_word = (uint16_t)(0u - (clock_ret->high >> 16) - borrow);
        clock_ret->high = ((uint32_t)top_word << 16) | (low32 >> 16);
        clock_ret->low = (uint16_t)low32;
    }
}

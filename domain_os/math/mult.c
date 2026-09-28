/*
 * math/mult.c - the M$MI* runtime multiply routines
 *
 * SAU2 map: segment M$ARITH at E0ABD4, size 0x1DC.  Four multiply entry
 * points, all hand-written stack-argument routines (no link frame,
 * arguments read straight off the caller's stack, result in D0):
 *
 *   M$MIS$LLL  0x00E0ABD4  46 bytes  signed   long * long -> long
 *   M$MIU$LLW  0x00E0AC02  26 bytes  unsigned long * word -> long
 *   M$MIU$LLL  0x00E0AC1C  40 bytes  unsigned long * long -> long
 *   M$MIS$LLW  0x00E0AC44  36 bytes  signed   long * word -> long
 *
 * The 68010 has no 32x32 multiply, so every product is assembled from
 * 16x16 `mulu`/`muls` partial products and only the low 32 bits survive.
 * All arithmetic below is on fixed-width unsigned types so a host of any
 * word size reproduces exactly that truncation.
 *
 * Re-checked against the disassembly 2026-09-27; the previous bodies were
 * right, this rewrite pins the widths and cites the instructions.
 */

#include "math/math_internal.h"

/*
 * M$MIU$LLL - unsigned long * long, low 32 bits (0x00E0AC1C)
 *
 *   0x00E0AC1C  move.w (0x4,SP),D0w / mulu.w (0xa,SP),D0   hi(mc) * lo(mul)
 *   0x00E0AC24  swap / clr.w / movea.l D0,A0               << 16
 *   0x00E0AC2A  move.w (0x8,SP),D0w / mulu.w (0x6,SP),D0   hi(mul) * lo(mc)
 *   0x00E0AC32  swap / clr.w / adda.l D0,A0                << 16, summed
 *   0x00E0AC38  move.w (0x6,SP),D0w / mulu.w (0xa,SP),D0   lo(mc) * lo(mul)
 *   0x00E0AC40  add.l A0,D0
 */
ulong M$MIU$LLL(ulong multiplicand, ulong multiplier)
{
    uint32_t mc = (uint32_t)multiplicand;
    uint32_t mul = (uint32_t)multiplier;
    uint32_t acc;

    acc = (uint32_t)(HIGH16(mc) * LOW16(mul)) << 16;         /* 0x00E0AC1C */
    acc += (uint32_t)(HIGH16(mul) * LOW16(mc)) << 16;        /* 0x00E0AC2A */
    acc += LOW16(mc) * LOW16(mul);                           /* 0x00E0AC38 */
    return acc;
}

/*
 * M$MIU$LLW - unsigned long * word, low 32 bits (0x00E0AC02)
 *
 *   0x00E0AC02  move.w (0x4,SP),D0w / mulu.w (0x8,SP),D0   hi(mc) * mul
 *   0x00E0AC0A  swap / clr.w / movea.l D0,A0               << 16
 *   0x00E0AC10  move.w (0x6,SP),D0w / mulu.w (0x8,SP),D0   lo(mc) * mul
 *   0x00E0AC18  add.l A0,D0
 */
ulong M$MIU$LLW(ulong multiplicand, ushort multiplier)
{
    uint32_t mc = (uint32_t)multiplicand;
    uint32_t mul = (uint32_t)(uint16_t)multiplier;

    return ((uint32_t)(HIGH16(mc) * mul) << 16) + LOW16(mc) * mul;
}

/*
 * M$MIS$LLL - signed long * long, low 32 bits (0x00E0ABD4)
 *
 *   0x00E0ABD4  clr.w -(SP)                    sign word = 0
 *   0x00E0ABD6  move.l (0x6,SP),-(SP) / bge    push multiplicand; if negative
 *   0x00E0ABDE    not.w (0x4,SP) / neg.l (SP)    flip the sign word, negate
 *   0x00E0ABE4  move.l (0xe,SP),-(SP) / bge    push multiplier; if negative
 *   0x00E0ABEC    not.w (0x8,SP) / neg.l (SP)    flip again, negate
 *   0x00E0ABF2  bsr M$MIU$LLL                  (multiplier, multiplicand)
 *   0x00E0ABF8  tst.w (SP)+ / bge / neg.l D0   negate if the sign word is set
 *
 * `neg.l` of 0x80000000 leaves it unchanged, exactly as -(int32_t) does on
 * a two's-complement machine when done in unsigned arithmetic.
 */
long M$MIS$LLL(long multiplicand, long multiplier)
{
    uint32_t mc = (uint32_t)multiplicand;
    uint32_t mul = (uint32_t)multiplier;
    uint16_t sign = 0;                                       /* 0x00E0ABD4 */
    uint32_t product;

    if ((int32_t)mc < 0) {                                   /* 0x00E0ABDA bge */
        sign = (uint16_t)~sign;
        mc = 0u - mc;
    }
    if ((int32_t)mul < 0) {                                  /* 0x00E0ABE8 bge */
        sign = (uint16_t)~sign;
        mul = 0u - mul;
    }
    product = (uint32_t)M$MIU$LLL(mul, mc);                  /* 0x00E0ABF2 */
    if ((int16_t)sign < 0) {                                 /* 0x00E0ABF8 */
        product = 0u - product;
    }
    return (long)(int32_t)product;
}

/*
 * M$MIS$LLW - signed long * word, low 32 bits (0x00E0AC44)
 *
 *   0x00E0AC44  move.w (0x4,SP),D0w / muls.w (0x8,SP),D0   hi(mc) * mul, signed
 *   0x00E0AC4C  tst.w (0x8,SP) / bpl                       multiplier negative:
 *   0x00E0AC52    sub.w (0x6,SP),D0w                         D0w -= lo(mc)
 *   0x00E0AC56  swap / clr.w / movea.l D0,A0               low WORD of that << 16
 *   0x00E0AC5C  move.w (0x6,SP),D0w / mulu.w (0x8,SP),D0   lo(mc) * (uint16)mul
 *   0x00E0AC64  add.l A0,D0
 *
 * Only the low word of the signed partial product survives the swap, so
 * the high half is computed modulo 2^16.
 */
long M$MIS$LLW(long multiplicand, short multiplier)
{
    uint32_t mc = (uint32_t)multiplicand;
    uint16_t hi;

    hi = (uint16_t)((int32_t)(int16_t)HIGH16(mc) * (int32_t)multiplier);   /* 0x00E0AC44 */
    if (multiplier < 0) {                                                  /* 0x00E0AC4C */
        hi = (uint16_t)(hi - LOW16(mc));                                   /* 0x00E0AC52 */
    }
    return (long)(int32_t)(((uint32_t)hi << 16) +
                           LOW16(mc) * (uint32_t)(uint16_t)multiplier);    /* 0x00E0AC5C */
}

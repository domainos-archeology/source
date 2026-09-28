/*
 * math/div.c - the M$DI* runtime divide routines
 *
 * SAU2 map: segment M$ARITH at E0ABD4, size 0x1DC.  Four divide entry
 * points, all hand-written stack-argument routines (no link frame,
 * arguments read straight off the caller's stack, quotient in D0):
 *
 *   M$DIU$LLW  0x00E0AC68  28 bytes  unsigned long / word -> long
 *   M$DIS$LLW  0x00E0AC84  60 bytes  signed   long / word -> long
 *   M$DIS$LLL  0x00E0ACD8  52 bytes  signed   long / long -> long
 *   M$DIU$LLL  0x00E0AD0C  72 bytes  unsigned long / long -> long
 *
 * A 32/16 divide is done in two `divu.w` steps (high word first, its
 * remainder feeding the low word); a 32/32 divide with a divisor above
 * 0xFFFF is a 32-step shift-and-subtract loop.  Division by zero traps on
 * the target and is not modelled.
 *
 * Re-emitted from the disassembly 2026-09-27: M$DIU$LLL's loop had an
 * uninitialised remainder and a spurious CONCAT; the other three were
 * right and are rewritten with fixed widths and citations.
 */

#include "math/math_internal.h"

/*
 * Two-stage 32/16 unsigned divide shared by the three word-divisor paths:
 *
 *   moveq #0,D0 / move.w hi,D0w / divu.w d,D0     D0 = rem1:q1
 *   movea.l D0,A0
 *   move.w lo,D0w / divu.w d,D0                   D0 = rem2:q2  (rem1:lo / d)
 *
 * q1 < 0x10000 (hi < 0x10000) and q2 < 0x10000 (rem1 < d), so neither
 * divu overflows.
 */
static inline uint32_t m$div32_16(uint32_t dividend, uint16_t divisor,
                                  uint16_t *q1_out, uint16_t *q2_out, uint16_t *rem2_out)
{
    uint32_t hi = HIGH16(dividend);
    uint32_t q1 = hi / divisor;
    uint32_t rem1 = hi % divisor;
    uint32_t stage2 = (rem1 << 16) | LOW16(dividend);
    uint32_t q2 = stage2 / divisor;
    uint32_t rem2 = stage2 % divisor;

    *q1_out = (uint16_t)q1;
    *q2_out = (uint16_t)q2;
    *rem2_out = (uint16_t)rem2;
    return ((uint32_t)(uint16_t)q1 << 16) | (uint16_t)q2;
}

/*
 * M$DIU$LLW - unsigned long / word (0x00E0AC68)
 *
 *   0x00E0AC68 .. 0x00E0AC78  the two divu steps on (0x4,SP)/(0x6,SP) by (0x8,SP)
 *   0x00E0AC7C  swap D0 / move.w A0w,D0w / swap D0   D0 = q1:q2
 */
ulong M$DIU$LLW(ulong dividend, ushort divisor)
{
    uint16_t q1, q2, rem2;

    return m$div32_16((uint32_t)dividend, (uint16_t)divisor, &q1, &q2, &rem2);
}

/*
 * M$DIS$LLW - signed long / word (0x00E0AC84)
 *
 *   0x00E0AC88  move.l (0xc,SP),D1 / bpl / neg.l D1     |dividend|
 *   0x00E0AC90  move.w (0x10,SP),D2w / bpl / neg.w D2w  |divisor| (word)
 *   0x00E0AC98 .. 0x00E0ACAC  the two divu steps by D2w, D0 = q1:q2
 *   0x00E0ACAE  move.w (0xc,SP),D1w                     high word of dividend
 *   0x00E0ACB2  eor.w D1w,(0x10,SP) / bpl / neg.l D0    signs differ -> negate
 *
 * The sign test is the XOR of the dividend's HIGH WORD with the divisor
 * word, i.e. of the two sign bits.
 */
long M$DIS$LLW(long dividend, short divisor)
{
    uint32_t dvd = (uint32_t)dividend;
    uint16_t dvs = (uint16_t)divisor;
    uint32_t q;
    uint16_t q1, q2, rem2;

    if ((int32_t)dvd < 0) {                                  /* 0x00E0AC8C */
        dvd = 0u - dvd;
    }
    if ((int16_t)dvs < 0) {                                  /* 0x00E0AC94 */
        dvs = (uint16_t)(0u - dvs);
    }
    q = m$div32_16(dvd, dvs, &q1, &q2, &rem2);               /* 0x00E0AC98 */
    if ((int16_t)(HIGH16((uint32_t)dividend) ^ (uint16_t)divisor) < 0) {   /* 0x00E0ACB2 */
        q = 0u - q;
    }
    return (long)(int32_t)q;
}

/*
 * M$DIU$LLL - unsigned long / long (0x00E0AD0C)
 *
 *   0x00E0AD0C  tst.w (0x8,SP) / bne          divisor high word zero:
 *   0x00E0AD12 .. 0x00E0AD2C    the two divu steps by (0xa,SP), D0 = q1:q2
 *   0x00E0AD2E  movem.l D1-D3 / clr.l D1      otherwise: remainder D1 = 0
 *   0x00E0AD34  move.l (0x10,SP),D0            D0 = dividend
 *   0x00E0AD38  move.l (0x14,SP),D2            D2 = divisor
 *   0x00E0AD3C  moveq #0x1f,D3                 32 iterations
 *   0x00E0AD3E    lsl.l #1,D0 / roxl.l #1,D1   shift the top bit of D0 into D1
 *   0x00E0AD42    cmp.l D2,D1 / bcs            D1 >= D2 (unsigned):
 *   0x00E0AD46      sub.l D2,D1 / addq.w #1,D0w  subtract, set quotient bit
 *   0x00E0AD4A  dbf D3w
 *
 * D1 is a 32-bit register, so a remainder with bit 31 set loses that bit
 * on the roxl; the uint32_t arithmetic below drops it the same way.
 */
ulong M$DIU$LLL(ulong dividend, ulong divisor)
{
    uint32_t d0 = (uint32_t)dividend;
    uint32_t d1;
    uint32_t d2 = (uint32_t)divisor;
    int16_t d3;
    uint32_t carry;
    uint16_t q1, q2, rem2;

    if (HIGH16(d2) == 0) {                                   /* 0x00E0AD0C */
        return m$div32_16(d0, (uint16_t)d2, &q1, &q2, &rem2);
    }

    d1 = 0;                                                  /* 0x00E0AD32 */
    for (d3 = 0x1f; d3 != -1; d3--) {                        /* 0x00E0AD3C */
        carry = d0 >> 31;
        d0 <<= 1;                                            /* 0x00E0AD3E */
        d1 = (d1 << 1) | carry;                              /* 0x00E0AD40 */
        if (d1 >= d2) {                                      /* 0x00E0AD42 bcs */
            d1 -= d2;                                        /* 0x00E0AD46 */
            d0 = (d0 & 0xFFFF0000u) | (uint16_t)(LOW16(d0) + 1);   /* 0x00E0AD48 addq.w */
        }
    }
    return d0;
}

/*
 * M$DIS$LLL - signed long / long (0x00E0ACD8)
 *
 *   0x00E0ACD8  move.l (0x8,SP),-(SP) / bmi    push divisor; negative -> 0x00E0ACEA
 *   0x00E0ACDE  move.l (0x8,SP),-(SP) / bmi    push dividend; negative -> 0x00E0AD02
 *   0x00E0ACE4  bsr M$DIU$LLL                  both positive: q
 *   0x00E0ACEA  neg.l (SP)                     divisor negated
 *   0x00E0ACEC  move.l (0x8,SP),-(SP) / bmi    push dividend; negative -> 0x00E0ACFA
 *   0x00E0ACF2  bsr / neg.l D0                 dividend positive: -q
 *   0x00E0ACFA  neg.l (SP) / bsr               both negative: q
 *   0x00E0AD02  neg.l (SP) / bsr / neg.l D0    dividend negative only: -q
 *
 * M$DIU$LLL receives (dividend, divisor) - the dividend is the last push.
 */
long M$DIS$LLL(long dividend, long divisor)
{
    uint32_t dvd = (uint32_t)dividend;
    uint32_t dvs = (uint32_t)divisor;
    uint32_t q;

    if ((int32_t)dvs < 0) {                                  /* 0x00E0ACDC bmi */
        dvs = 0u - dvs;                                      /* 0x00E0ACEA */
        if ((int32_t)dvd < 0) {                              /* 0x00E0ACF0 bmi */
            dvd = 0u - dvd;                                  /* 0x00E0ACFA */
            q = (uint32_t)M$DIU$LLL(dvd, dvs);
            return (long)(int32_t)q;
        }
        q = (uint32_t)M$DIU$LLL(dvd, dvs);                   /* 0x00E0ACF2 */
        return (long)(int32_t)(0u - q);
    }
    if ((int32_t)dvd < 0) {                                  /* 0x00E0ACE2 bmi */
        dvd = 0u - dvd;                                      /* 0x00E0AD02 */
        q = (uint32_t)M$DIU$LLL(dvd, dvs);
        return (long)(int32_t)(0u - q);
    }
    q = (uint32_t)M$DIU$LLL(dvd, dvs);                       /* 0x00E0ACE4 */
    return (long)(int32_t)q;
}

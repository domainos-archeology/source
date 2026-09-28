/*
 * math/mod.c - the M$OI* runtime remainder routines
 *
 * SAU2 map: segment M$ARITH at E0ABD4, size 0x1DC.  Four remainder entry
 * points, all hand-written stack-argument routines (no link frame,
 * arguments read straight off the caller's stack, remainder in D0):
 *
 *   M$OIU$WLW  0x00E0ACC0  24 bytes  unsigned long mod word -> word
 *   M$OIS$WLW  0x00E0AD54  20 bytes  signed   long mod word -> word
 *   M$OIS$WWL  0x00E0AD68  20 bytes  signed   word mod long -> word
 *   M$OIS$LLL  0x00E0AD7C  52 bytes  signed   long mod long -> long
 *              (the map lists M$OIU$LLL at the same address)
 *
 * Re-checked against the disassembly 2026-09-27; the bodies were right and
 * are rewritten with fixed widths and citations.
 */

#include "math/math_internal.h"

/*
 * M$OIS$LLL - signed long mod long (0x00E0AD7C)
 *
 *   0x00E0AD80  move.l (0x10,SP),D1 / bpl / neg.l D1    |dividend|
 *   0x00E0AD88  move.l (0x14,SP),D2 / bpl / neg.l D2    |divisor|
 *   0x00E0AD90  moveq #0x1f,D3 / moveq #0,D0            32 iterations, rem = 0
 *   0x00E0AD94    lsl.l #1,D1 / roxl.l #1,D0            shift the top bit in
 *   0x00E0AD98    cmp.l D2,D0 / bcs / sub.l D2,D0       D0 >= D2 -> subtract
 *   0x00E0AD9E  dbf D3w
 *   0x00E0ADA2  tst.w (0x10,SP) / bpl / neg.l D0        dividend's high word
 *                                                       negative -> negate
 *
 * Only the remainder is kept (no quotient bit is set).  As in M$DIU$LLL
 * the remainder register is 32 bits wide, so a bit shifted out of bit 31
 * is lost; the uint32_t arithmetic drops it the same way.
 */
long M$OIS$LLL(long dividend, long divisor)
{
    uint32_t d1 = (uint32_t)dividend;
    uint32_t d2 = (uint32_t)divisor;
    uint32_t d0;
    int16_t d3;
    uint32_t carry;

    if ((int32_t)d1 < 0) {                                   /* 0x00E0AD84 */
        d1 = 0u - d1;
    }
    if ((int32_t)d2 < 0) {                                   /* 0x00E0AD8C */
        d2 = 0u - d2;
    }
    d0 = 0;
    for (d3 = 0x1f; d3 != -1; d3--) {                        /* 0x00E0AD90 */
        carry = d1 >> 31;
        d1 <<= 1;                                            /* 0x00E0AD94 */
        d0 = (d0 << 1) | carry;                              /* 0x00E0AD96 */
        if (d0 >= d2) {                                      /* 0x00E0AD98 bcs */
            d0 -= d2;                                        /* 0x00E0AD9C */
        }
    }
    if ((int16_t)HIGH16((uint32_t)dividend) < 0) {           /* 0x00E0ADA2 */
        d0 = 0u - d0;
    }
    return (long)(int32_t)d0;
}

/*
 * M$OIS$WLW - signed long mod word (0x00E0AD54)
 *
 *   0x00E0AD54  move.w (0x8,SP),D0w / ext.l D0 / move.l D0,-(SP)   divisor, sign-extended
 *   0x00E0AD5C  move.l (0x8,SP),-(SP)                            dividend
 *   0x00E0AD60  bsr M$OIS$LLL / addq.w #8,SP                     result low word
 */
short M$OIS$WLW(long dividend, short divisor)
{
    return (short)(int16_t)M$OIS$LLL(dividend, (long)(int32_t)divisor);
}

/*
 * M$OIS$WWL - signed word mod long (0x00E0AD68)
 *
 *   0x00E0AD68  move.l (0x6,SP),-(SP)                            divisor
 *   0x00E0AD6C  move.w (0x8,SP),D0w / ext.l D0 / move.l D0,-(SP) dividend, sign-extended
 *   0x00E0AD74  bsr M$OIS$LLL / addq.w #8,SP                     result low word
 */
short M$OIS$WWL(short dividend, long divisor)
{
    return (short)(int16_t)M$OIS$LLL((long)(int32_t)dividend, divisor);
}

/*
 * M$OIU$WLW - unsigned long mod word (0x00E0ACC0)
 *
 *   0x00E0ACC0  moveq #0,D0 / move.w (0x4,SP),D0w / divu.w (0x8,SP),D0   rem1:q1
 *   0x00E0ACCA  movea.l D0,A0
 *   0x00E0ACCC  move.w (0x6,SP),D0w / divu.w (0x8,SP),D0                 rem2:q2
 *   0x00E0ACD4  swap D0                                                  D0w = rem2
 */
short M$OIU$WLW(long dividend, short divisor)
{
    uint32_t dvd = (uint32_t)dividend;
    uint32_t dvs = (uint32_t)(uint16_t)divisor;
    uint32_t rem1 = HIGH16(dvd) % dvs;                       /* 0x00E0ACC6 */
    uint32_t stage2 = (rem1 << 16) | LOW16(dvd);             /* 0x00E0ACCC */

    return (short)(uint16_t)(stage2 % dvs);                  /* 0x00E0ACD4 */
}

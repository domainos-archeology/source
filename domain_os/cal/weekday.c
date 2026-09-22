/*
 * CAL_$WEEKDAY - Day of the week for a calendar date
 *
 * Zeller-style congruence: the year is stepped back one for January and
 * February, the leap-year terms y/4 - y/100 + y/400 are added, the month
 * term is ((month + 9) mod 12 * 153 + 2) / 5, and (sum + day + 1) mod 7 is
 * the result, 0 = Sunday.
 *
 * Parameters:
 *   year  - (0x8,A6), A0
 *   month - (0xc,A6), A1; 1..12
 *   day   - (0x10,A6)
 *
 * Returns (D0w): 0..6.  The Pascal remainder is SIGNED, so a negative
 * intermediate would come back negative (the `bcc` guard below passes it
 * through untouched).
 *
 * Original address: 0x00e68670, 188 bytes
 *
 *   00e68680  cmpi.w #0x3,(A1) / blt          ; month < 3 -> year - 1
 *   00e6868e  D2 = ext.l y; D0 = D2 divs.w #100
 *   00e68698  D3 = y; bpl / addq.w #3; asr.w #2     ; y div 4 toward zero
 *   00e686a0  D3 += y; D3 += 1; D3 -= y/100
 *   00e686a6  D0 = D2 divs.w #400; D3 += D0w
 *   00e686ae  D0 = ext.l month + 9; M$OIS$WLW(D0, 12)
 *   00e686c8  muls.w #0x99 / ext.l D0 (of the LOW word) / addq.l #2 / divs.w #5
 *   00e686d8  D3 += D0w
 *   00e686da  D1 = ext.l D3 + ext.l day + 1; M$OIS$WLW(D1, 7)
 *   00e686f8  cmpi.w #0x7,D0w / bcc -> return D0 as is   (UNSIGNED)
 *   00e686fe  jump table over 0..6, each arm loading the same value
 */

#include "cal/cal_internal.h"

short CAL_$WEEKDAY(short *year, short *month, short *day)
{
    int16_t y;              /* D1w */
    int32_t y32;            /* D2 */
    int16_t sum;            /* D3w */
    int16_t quarter;
    int16_t month_term;
    int32_t month_index;    /* D0 */
    int32_t dividend;       /* D1 */
    int16_t result;         /* D0w */

    /* 0x00E68680..0x00E6868C */
    if (*month < 3) {
        y = (int16_t)(*year - 1);
    } else {
        y = *year;
    }
    y32 = y;

    /* 0x00E68698..0x00E6869E: asr of (y + 3 if negative) = y div 4 */
    quarter = y;
    if (quarter < 0) {
        quarter = (int16_t)(quarter + 3);
    }
    quarter = (int16_t)(quarter >> 2);

    /* 0x00E686A0..0x00E686AC */
    sum = (int16_t)(quarter + y);
    sum = (int16_t)(sum + 1);
    sum = (int16_t)(sum - (int16_t)(y32 / 100));
    sum = (int16_t)(sum + (int16_t)(y32 / 400));

    /* 0x00E686AE..0x00E686C6: (month + 9) mod 12, signed */
    month_index = (int32_t)*month + 9;
    month_term = M$OIS$WLW(month_index, 12);

    /* 0x00E686C8..0x00E686D8: the muls result is re-sign-extended from 16 bits */
    month_term = (int16_t)(month_term * 0x99);
    sum = (int16_t)(sum + (int16_t)(((int32_t)month_term + 2) / 5));

    /* 0x00E686DA..0x00E686F4 */
    dividend = (int32_t)*day + (int32_t)sum + 1;
    result = M$OIS$WLW(dividend, 7);

    /* 0x00E686F8..0x00E6872E: an unsigned guard, then an identity jump table */
    if ((uint16_t)result >= 7) {
        return result;
    }
    switch (result) {
    case 0: return 0;
    case 1: return 1;
    case 2: return 2;
    case 3: return 3;
    case 4: return 4;
    case 5: return 5;
    default: return 6;
    }
}

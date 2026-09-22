/*
 * CAL_$DECODE_TIME - 48-bit clock -> {year, month, day, hour, minute, second}
 *
 * Converts the clock to seconds with CAL_$CLOCK_TO_SEC, peels off seconds,
 * minutes and hours with the unsigned runtime mod/div, then walks whole
 * years from 1980 (a leap year) and whole months through a frame copy of
 * CAL_$DAYS_PER_MONTH whose February entry tracks the current year.
 *
 * Parameters:
 *   clock    - the clock to decode ((0x8,A6))
 *   time_rec - six words ((0xc,A6), A2): [0] year, [1] month 1..12,
 *              [2] day 1..31, [3] hour, [4] minute, [5] second
 *
 * Original address: 0x00e6873a, 298 bytes
 *
 * Frame: -0x18..-0x01  local_days[12] (A3 = A6+2, stored via (-0x1a,A0));
 *        -0x16 is local_days[1], the February entry.
 *
 *   00e68746  moveq #0xb,D0 / ... / dbf    ; 12 words copied from 0xE817AC
 *   00e6876a  jsr CAL_$CLOCK_TO_SEC(clock) -> D1
 *   00e68778  M$OIU$WLW(D1, 60) -> (0xa,A2) second; M$DIU$LLW(D1, 60) -> D1
 *   00e6879e  M$OIU$WLW(D1, 60) -> (0x8,A2) minute; M$DIU$LLW(D1, 60) -> D1
 *   00e687c4  M$OIU$WLW(D1, 24) -> (0x6,A2) hour;   M$DIU$LLW(D1, 24) -> D1
 *   00e687ea  D0w = 366; D1 += 1; local_days[1] = 29; year = 1980
 *   00e68824  D2 = ext.l D0w; cmp.l D1,D2; blt -> body:
 *   00e687fc    D1 -= D2; year++; divs.w #4 / swap / tst.w remainder:
 *               non-zero -> 365 & Feb 28, zero -> 366 & Feb 29
 *   00e6882c  D3 = 0 (cumulative), D2 = 11 (dbf), D0w = 1 (month), A0 = local_days
 *   00e68834    D4 = ext D3 + ext local_days[i]; cmp.l D1,D4; bge -> exit
 *               D3 += local_days[i]; month++; A0 += 2; dbf
 *   00e68850  (0x2,A2) = month; (0x4,A2) = D1w - D3w
 *
 * The month loop runs at most 12 times and can leave month == 13 when the
 * day count exceeds the year (it cannot after the year loop, but nothing in
 * the code prevents it); reproduced as is.
 */

#include "cal/cal_internal.h"

void CAL_$DECODE_TIME(clock_t *clock, short *time_rec)
{
    int16_t local_days[12];     /* A6-0x18 */
    uint32_t remaining;         /* D1 */
    int16_t days_in_year;       /* D0w in the year loop */
    int16_t cumulative;         /* D3w */
    int16_t month;              /* D0w in the month loop */
    int16_t count;
    int i;

    /* 0x00E68746..0x00E68766: moveq #0xb / dbf = 12 words */
    for (count = 0x0B, i = 0; count >= 0; count--, i++) {
        local_days[i] = CAL_$DAYS_PER_MONTH[i];
    }

    /* 0x00E6876A..0x00E68776 */
    remaining = (uint32_t)CAL_$CLOCK_TO_SEC(clock);

    /* 0x00E68778..0x00E6879C */
    time_rec[5] = M$OIU$WLW((long)remaining, 60);
    remaining = (uint32_t)M$DIU$LLW(remaining, 60);

    /* 0x00E6879E..0x00E687C2 */
    time_rec[4] = M$OIU$WLW((long)remaining, 60);
    remaining = (uint32_t)M$DIU$LLW(remaining, 60);

    /* 0x00E687C4..0x00E687E8 */
    time_rec[3] = M$OIU$WLW((long)remaining, 24);
    remaining = (uint32_t)M$DIU$LLW(remaining, 24);

    /* 0x00E687EA..0x00E687F6: 1980 is a leap year; day numbers are 1-based */
    days_in_year = 366;
    remaining += 1;
    local_days[1] = 29;
    time_rec[0] = 1980;

    /* 0x00E68824..0x00E6882A: cmp.l D1,D2 / blt (SIGNED, 32-bit) */
    while ((int32_t)days_in_year < (int32_t)remaining) {
        /* 0x00E687FC..0x00E687FE */
        remaining -= (uint32_t)(int32_t)days_in_year;
        time_rec[0] = (short)(time_rec[0] + 1);

        /* 0x00E68800..0x00E6881E: divs.w #4, remainder in the swapped half */
        if ((time_rec[0] % 4) != 0) {
            days_in_year = 365;
            local_days[1] = 28;
        } else {
            days_in_year = 366;
            local_days[1] = 29;
        }
    }

    /* 0x00E6882C..0x00E6884C: dbf over the 12 months */
    cumulative = 0;
    month = 1;
    for (count = 0x0B, i = 0; count >= 0; count--, i++) {
        /* cmp.l D1,D4 / bge: exit once cumulative + this month reaches the day */
        if ((int32_t)cumulative + (int32_t)local_days[i] >= (int32_t)remaining) {
            break;
        }
        cumulative = (int16_t)(cumulative + local_days[i]);
        month = (int16_t)(month + 1);
    }

    /* 0x00E68850..0x00E68856 */
    time_rec[1] = month;
    time_rec[2] = (short)((int16_t)remaining - cumulative);
}

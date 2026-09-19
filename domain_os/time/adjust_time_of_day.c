/*
 * TIME_$ADJUST_TIME_OF_DAY - Adjust the time of day gradually
 *
 * The Domain/OS equivalent of adjtime(): the requested delta is converted
 * to 4-microsecond ticks, rounded to a multiple of the skew step, and armed
 * as TIME_$CURRENT_SKEW / TIME_$CURRENT_TICK / TIME_$CURRENT_DELTA for the
 * timer interrupt to work off.  The calendar chip is rewritten with the
 * adjusted time of day, and the delta that was pending before the call is
 * returned split into seconds and microseconds.
 *
 * Parameters (frame 0x00E168E6..0x00E168EE):
 *   0x08 delta     - {seconds, microseconds} to add (A2)
 *   0x0C old_delta - receives the previously pending {seconds, microseconds}
 *                    (A3); written unconditionally, never checked for nil
 *   0x10 status    - status return (A0)
 *
 * Original address: 0x00e168de, 458 bytes
 *
 * Frame locals:
 *   -0x3C  unix_secs   longword handed to CAL_$SEC_TO_CLOCK
 *   -0x32  weekday     word from CAL_$WEEKDAY
 *   -0x28  new_clock   clock_t
 *   -0x20  usec_ticks  6-byte record: cleared word at -0x20, longword at -0x1E
 *   -0x18  tv          {seconds at -0x18, microseconds at -0x14}
 *   -0x10  decoded     6 words: year, month, day, hour, minute, second
 */

#include "time/time_internal.h"
#include "math/math.h"

void TIME_$ADJUST_TIME_OF_DAY(int32_t *delta, int32_t *old_delta, status_$t *status)
{
    int32_t magnitude;          /* D0 at 0x00E168F4, D1 at 0x00E1692C */
    int32_t usec_ticks_in;      /* D1 at 0x00E1691C */
    int32_t delta_ticks;        /* D2 */
    int16_t skew;               /* D3w */
    int16_t remainder;          /* D0w at 0x00E16958 */
    int32_t quotient;           /* D0 at 0x00E16968 */
    int16_t tick;               /* D4w */
    int32_t old_delta_ticks;    /* D3 from 0x00E169A2 on */
    int32_t usec_ticks_count;   /* D0 at 0x00E16A0A */
    uint32_t tv[2];             /* A6-0x18 */
    uint unix_secs;             /* A6-0x3C */
    clock_t new_clock;          /* A6-0x28 */
    clock_t usec_ticks;         /* A6-0x20 */
    int16_t decoded[6];         /* A6-0x10 */
    int16_t weekday;            /* A6-0x32 */

    /* 0x00E168EE..0x00E168F2: clr.l (A0) */
    *status = status_$ok;

    /*
     * 0x00E168F4..0x00E16908: |delta seconds| compared UNSIGNED against
     * 0x1F40 (8000): `cmpi.l #0x1f40,D0 / bls`.  -2^31 negates to itself
     * and therefore fails the bound.
     */
    magnitude = delta[0];
    if (magnitude < 0) {
        magnitude = -magnitude;
    }
    if ((uint32_t)magnitude > (uint32_t)MAX_ADJUST_SECONDS) {
        *status = status_$time_adjustment_out_of_range;    /* 0x000D000C */
        return;                                             /* bra.w 0x00e16a9e */
    }

    /*
     * 0x00E1690C..0x00E16928: ticks = seconds * 250000 (M$MIS$LLL) +
     * microseconds div 4, the division truncating toward zero
     * (bpl / addq.l #0x3 / asr.l #0x2).
     */
    usec_ticks_in = delta[1];
    if (usec_ticks_in < 0) {
        usec_ticks_in += 3;
    }
    usec_ticks_in >>= 2;
    delta_ticks = (int32_t)M$MIS$LLL(delta[0], TICKS_PER_SECOND) + usec_ticks_in;

    /* 0x00E1692A: beq.b 0x00e1697e - a zero delta gets no skew */
    if (delta_ticks != 0) {
        /*
         * 0x00E1692C..0x00E16940: |ticks| compared UNSIGNED against 250000
         * picks the skew step: 0xA7 (167) for up to one second, 0x686
         * (1670) beyond.
         */
        magnitude = delta_ticks;
        if (magnitude < 0) {
            magnitude = -magnitude;
        }
        if ((uint32_t)magnitude > (uint32_t)TICKS_PER_SECOND) {
            skew = SKEW_DIVISOR_FAST;
        } else {
            skew = SKEW_DIVISOR_SLOW;
        }

        /* 0x00E16944..0x00E16948: tst.l D2 / bpl / neg.w D3w */
        if (delta_ticks < 0) {
            skew = (int16_t)-skew;
        }

        /*
         * 0x00E1694A..0x00E16978: if ticks mod skew (M$OIS$WLW) is not
         * zero, round ticks toward zero to a multiple of skew with
         * M$DIS$LLW then M$MIS$LLW.
         */
        remainder = M$OIS$WLW(delta_ticks, skew);
        if (remainder != 0) {
            quotient = (int32_t)M$DIS$LLW(delta_ticks, skew);
            delta_ticks = (int32_t)M$MIS$LLW(quotient, skew);
        }
    }

    /* 0x00E1697A..0x00E1697E: tst.l D2 / bne / clr.w D3w */
    if (delta_ticks == 0) {
        skew = 0;
    }

    /* 0x00E16980..0x00E16982: D4w = skew + 0x1047 */
    tick = (int16_t)(skew + TIME_INITIAL_TICK);

    /* 0x00E16986..0x00E16990: TIME_$GET_TIME_OF_DAY(&tv) */
    TIME_$GET_TIME_OF_DAY(tv);

    /* 0x00E16992: ori #0x700,SR - raised, nothing saved */
    SET_IPL7();
    TIME_$CURRENT_SKEW = (uint16_t)skew;                    /* 0x00E16996 */
    TIME_$CURRENT_TICK = (uint16_t)tick;                    /* 0x00E1699C */
    old_delta_ticks = (int32_t)TIME_$CURRENT_DELTA;         /* 0x00E169A2 */
    TIME_$CURRENT_DELTA = (uint32_t)delta_ticks;            /* 0x00E169A8 */
    /* 0x00E169AE: andi #0xf8ff,SR - FORCES IPL 0, not a restore */
    SET_IPL0();

    /* 0x00E169B2: tst.l D2 / beq.b 0x00e169e8 */
    if (delta_ticks != 0) {
        /* 0x00E169B6..0x00E169C0: add the raw delta to the time of day */
        tv[0] += (uint32_t)delta[0];
        tv[1] += (uint32_t)delta[1];

        /* 0x00E169C4: bpl on the result of the microsecond add */
        if ((int32_t)tv[1] < 0) {
            /* 0x00E169C6..0x00E169D2 */
            tv[1] += 1000000;
            tv[0] -= 1;
        } else {
            /* 0x00E169D4..0x00E169E4: cmp.l (-0x14,A6),D0 / bgt (signed) */
            if (!(1000000 > (int32_t)tv[1])) {
                tv[1] -= 1000000;
                tv[0] += 1;
            }
        }
    }

    /*
     * 0x00E169E8..0x00E16A04: on EVERY non-error path, delta or no delta,
     * rebuild the clock from the (possibly adjusted) time of day.  Seconds
     * are rebased from the Unix epoch to the Apollo one.
     */
    unix_secs = tv[0] - APOLLO_EPOCH_OFFSET;
    CAL_$SEC_TO_CLOCK(&unix_secs, &new_clock);

    /*
     * 0x00E16A06..0x00E16A26: clr.w (-0x20,A6) then the 32-bit tick count
     * at (-0x1e,A6), straddling the record's high/low split; the divide by
     * 4 is signed and truncates toward zero.  ADD48(&new_clock, &usec_ticks).
     */
    usec_ticks_count = (int32_t)tv[1];
    if (usec_ticks_count < 0) {
        usec_ticks_count += 3;
    }
    usec_ticks_count >>= 2;
    usec_ticks.high = (uint32_t)usec_ticks_count >> 16;
    usec_ticks.low = (uint16_t)((uint32_t)usec_ticks_count & 0xFFFFu);
    ADD48(&new_clock, &usec_ticks);

    /* 0x00E16A28..0x00E16A36 */
    CAL_$DECODE_TIME(&new_clock, decoded);

    /* 0x00E16A38..0x00E16A4E */
    weekday = CAL_$WEEKDAY(&decoded[0], &decoded[1], &decoded[2]);

    /* 0x00E16A52..0x00E16A74: year, month, day, weekday, hour, minute, second */
    CAL_$WRITE_CALENDAR(&decoded[0], &decoded[1], &decoded[2], &weekday,
                        &decoded[3], &decoded[4], &decoded[5]);

    /*
     * 0x00E16A78..0x00E16A9A: the previously pending delta goes back as
     * {ticks div 250000, (ticks mod 250000) * 4}; the last call's argument
     * bytes are never popped (unlk reclaims them).
     */
    old_delta[0] = (int32_t)M$DIS$LLL(old_delta_ticks, TICKS_PER_SECOND);
    old_delta[1] = (int32_t)((uint32_t)M$OIS$LLL(old_delta_ticks, TICKS_PER_SECOND) << 2);

    /* 0x00E16A9E: movem.l (-0x50,A6),{D2 D3 D4 A2 A3} / unlk / rts */
}

/*
 * TIME_$ADJUST_TIME_OF_DAY - Adjust time of day gradually
 *
 * Adjusts the system time gradually rather than jumping.
 * This is the Domain/OS equivalent of adjtime().
 *
 * Parameters:
 *   delta - Pointer to adjustment delta (seconds, microseconds)
 *   old_delta - Pointer to receive previous adjustment (may be NULL)
 *   status - Status return
 *
 * Original address: 0x00e168de
 *
 * The function:
 * 1. Converts delta to ticks
 * 2. Calculates skew value for gradual adjustment
 * 3. Updates TIME_$CURRENT_TICK and TIME_$CURRENT_SKEW
 * 4. Updates hardware RTC
 *
 * Assembly:
 *   00e168e6  movea.l (0x8,A6),A2 / movea.l (0xc,A6),A3
 *   00e168ee  movea.l (0x10,A6),A0 / clr.l (A0)
 *   00e168f4  abs(delta[0]) / cmpi.l #0x1f40 / bls  ; UNSIGNED bound, 8000
 *   00e16902  0xd000c
 *   00e1690c  jsr M$MIS$LLL(delta[0], 0x3d090)      ; * 250000
 *   00e1691c  move.l (0x4,A2),D1 / bpl / addq.l #0x3 / asr.l #0x2
 *   00e16932  cmpi.l #0x3d090 / bls -> 0xa7 else 0x686
 *   00e16944  tst.l D2 / bpl / neg.w D3w
 *   00e16950  jsr M$OIS$WLW / M$DIS$LLW / M$MIS$LLW ; round to a multiple
 *   00e1697e  clr.w D3w                             ; a zero delta has no skew
 *   00e16982  D4 = skew + 0x1047
 *   00e1698a  jsr TIME_$GET_TIME_OF_DAY(-0x18)
 *   00e16992  ori #0x700,SR                         ; raise, nothing saved
 *   00e16996..00e169a8  skew, tick and delta; D3 takes the OLD delta
 *   00e169ae  andi #-0x701,SR                       ; force IPL 0
 *   00e169b2  tst.l D2 / beq                        ; a zero delta skips the
 *                                                   ; time-of-day adjustment
 *   00e169b6  move.l (A2),D2                        ; D2 is reused here
 *   00e169e8..00e16a20  the clock is rebuilt and the RTC rewritten on EVERY
 *                       non-error path, delta or no delta
 *   00e16a30  jsr CAL_$DECODE_TIME
 *   00e16a44  jsr CAL_$WEEKDAY
 *   00e16a6e  jsr CAL_$WRITE_CALENDAR
 *   00e16a80  jsr M$DIS$LLL(old, 0x3d090)  -> old_delta[0]
 *   00e16a92  jsr M$OIS$LLL(old, 0x3d090) / lsl.l #0x2 -> old_delta[1]
 *
 * old_delta (A3) is loaded at entry and written unconditionally; the original
 * never checks it for nil.
 */

#include "time/time_internal.h"

/* Maximum adjustment allowed (8000 seconds) */
#define MAX_ADJUST_SECONDS 8000

/* Ticks per second */
#define TICKS_PER_SECOND 250000

/* Skew divisors for slow/fast adjustment */
#define SKEW_DIVISOR_SLOW 0x00A7   /* 167 - for small adjustments */
#define SKEW_DIVISOR_FAST 0x0686   /* 1670 - for large adjustments */

/* Status code for adjustment too large: "OS / time manager: bus time-out"
 * is 0x0012000C; this one is subsystem 0x0D and the database has no text
 * past 0x000D000B, so the constant is reproduced literally (0x00E16902). */
#define status_$time_adjust_too_large 0x000D000C

void TIME_$ADJUST_TIME_OF_DAY(int32_t *delta, int32_t *old_delta, status_$t *status)
{
    int32_t delta_secs;
    int32_t delta_usecs;
    int32_t delta_ticks;
    int32_t abs_ticks;
    int16_t skew;
    int16_t divisor;
    int32_t old_delta_ticks;
    uint32_t unix_secs;
    uint32_t usec_ticks_count;
    clock_t new_clock;          /* A6-0x28 */
    clock_t usec_ticks;         /* A6-0x20 */
    int16_t decoded[6];         /* A6-0x10: year, month, day, hour, min, sec */
    int16_t weekday;            /* A6-0x32 */
    uint32_t tv[2];             /* A6-0x18 */

    *status = status_$ok;

    delta_secs = delta[0];
    delta_usecs = delta[1];

    /* Check if delta is within allowed range */
    abs_ticks = delta_secs;
    if (abs_ticks < 0) {
        abs_ticks = -abs_ticks;
    }
    if (abs_ticks > MAX_ADJUST_SECONDS) {
        *status = status_$time_adjust_too_large;
        return;
    }

    /* Convert delta to ticks */
    delta_ticks = (delta_secs * TICKS_PER_SECOND) + (delta_usecs / 4);

    if (delta_ticks != 0) {
        /* Calculate absolute value for divisor selection */
        abs_ticks = delta_ticks;
        if (abs_ticks < 0) {
            abs_ticks = -abs_ticks;
        }

        /* Select divisor based on magnitude */
        if (abs_ticks <= TICKS_PER_SECOND) {
            divisor = SKEW_DIVISOR_SLOW;
        } else {
            divisor = SKEW_DIVISOR_FAST;
        }

        /* Negate divisor if delta is negative */
        if (delta_ticks < 0) {
            divisor = -divisor;
        }

        /* Check for zero remainder after division */
        skew = delta_ticks % divisor;
        if (skew != 0) {
            /* Adjust delta_ticks to be exact multiple of divisor */
            int32_t quotient = delta_ticks / divisor;
            delta_ticks = quotient * divisor;
        }

        if (delta_ticks == 0) {
            divisor = 0;
        }
    } else {
        divisor = 0;
    }

    skew = divisor;

    /* Get current time of day */
    TIME_$GET_TIME_OF_DAY(tv);

    /* 0xE16992: a bare "ori #0x700,SR" - nothing is saved */
    SET_IPL7();

    TIME_$CURRENT_SKEW = (uint16_t)skew;                    /* 0xE16996 */
    TIME_$CURRENT_TICK = (uint16_t)(TIME_INITIAL_TICK + skew); /* 0xE1699C */
    old_delta_ticks = (int32_t)TIME_$CURRENT_DELTA;         /* 0xE169A2 */
    TIME_$CURRENT_DELTA = (uint32_t)delta_ticks;            /* 0xE169A8 */

    /* 0xE169AE: "andi #-0x701,SR" forces IPL 0; it does not restore */
    SET_IPL0();

    /* If delta is non-zero, adjust current time */
    if (delta_ticks != 0) {
        tv[0] += delta_secs;
        tv[1] += delta_usecs;

        /* Normalize microseconds */
        if ((int32_t)tv[1] < 0) {
            tv[1] += 1000000;
            tv[0]--;
        } else if (tv[1] >= 1000000) {
            tv[1] -= 1000000;
            tv[0]++;
        }
    }

    /*
     * 0xE169E8: the clock is rebuilt and written to the RTC on every
     * non-error path, whether or not the delta was zero.
     */
    unix_secs = tv[0] - 0x12CEA600;  /* Convert to Apollo epoch */
    CAL_$SEC_TO_CLOCK(&unix_secs, &new_clock);

    /*
     * 0xE16A06/0xE16A14: the word at +0 is cleared and the 32-bit tick count
     * is stored at +2, straddling the record's high/low split, so counts
     * above 0xFFFF reach `high`.  The divide is SIGNED and rounds toward
     * zero (bpl / addq.l #3 / asr.l #2).
     */
    usec_ticks_count = (uint32_t)((int32_t)tv[1] / 4);
    usec_ticks.high = usec_ticks_count >> 16;
    usec_ticks.low = (uint16_t)(usec_ticks_count & 0xFFFFu);
    ADD48(&new_clock, &usec_ticks);

    /* 0xE16A30 */
    CAL_$DECODE_TIME(&new_clock, decoded);

    /* 0xE16A44 */
    weekday = CAL_$WEEKDAY(&decoded[0], &decoded[1], &decoded[2]);

    /* 0xE16A6E */
    CAL_$WRITE_CALENDAR(&decoded[0], &decoded[1], &decoded[2], &weekday,
                        &decoded[3], &decoded[4], &decoded[5]);

    /* 0xE16A78 - 0xE16A9A: written unconditionally, no nil check */
    old_delta[0] = old_delta_ticks / TICKS_PER_SECOND;
    old_delta[1] = (old_delta_ticks % TICKS_PER_SECOND) * 4;
}

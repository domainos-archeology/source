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
 */

#include "time.h"
#include "cal/cal.h"
#include "arch/m68k/arch.h"

/* Maximum adjustment allowed (8000 seconds) */
#define MAX_ADJUST_SECONDS 8000

/* Ticks per second */
#define TICKS_PER_SECOND 250000

/* Skew divisors for slow/fast adjustment */
#define SKEW_DIVISOR_SLOW 0x00A7   /* 167 - for small adjustments */
#define SKEW_DIVISOR_FAST 0x0686   /* 1670 - for large adjustments */

/* Status code for adjustment too large */
#define status_$time_adjust_too_large 0x000D000C

void TIME_$ADJUST_TIME_OF_DAY(int32_t *delta, int32_t *old_delta, status_$t *status)
{
    int32_t delta_secs;
    int32_t delta_usecs;
    int32_t delta_ticks;
    int32_t abs_ticks;
    int16_t skew;
    int16_t divisor;
    uint16_t saved_sr;
    int32_t old_delta_ticks;
    uint32_t unix_secs;
    clock_t new_clock;
    clock_t usec_ticks;
    uint32_t tv[2];

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

    /* Save old delta and set new one */
    GET_SR(saved_sr);
    SET_SR(saved_sr | SR_IPL_DISABLE_ALL);

    old_delta_ticks = (int32_t)TIME_$CURRENT_DELTA;
    TIME_$CURRENT_SKEW = (uint16_t)skew;
    TIME_$CURRENT_TICK = TIME_INITIAL_TICK + skew;
    TIME_$CURRENT_DELTA = (uint32_t)delta_ticks;

    SET_SR(saved_sr);

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

    /* Convert to clock and update RTC */
    unix_secs = tv[0] - 0x12CEA600;  /* Convert to Apollo epoch */
    CAL_$SEC_TO_CLOCK(&unix_secs, &new_clock);
    usec_ticks.high = 0;
    usec_ticks.low = (uint16_t)(tv[1] / 4);
    ADD48(&new_clock, &usec_ticks);

    /* TODO: Update hardware RTC with CAL_$DECODE_TIME and CAL_$WRITE_CALENDAR */

    /* Return old delta in seconds and microseconds */
    if (old_delta != NULL) {
        old_delta[0] = old_delta_ticks / TICKS_PER_SECOND;
        old_delta[1] = (old_delta_ticks % TICKS_PER_SECOND) * 4;
    }
}

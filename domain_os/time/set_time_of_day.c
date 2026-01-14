/*
 * TIME_$SET_TIME_OF_DAY - Set current time of day
 *
 * Sets the system time from a Unix timeval structure.
 * This affects all clock values and updates the hardware RTC.
 *
 * Parameters:
 *   tv - Pointer to timeval (tv[0]=seconds, tv[1]=microseconds)
 *   status - Status return
 *
 * Original address: 0x00e1678c
 *
 * The function:
 * 1. If time < Apollo epoch (0x12CEA600), just sets current time directly
 * 2. Otherwise, converts to clock ticks, adjusts boot time, and updates RTC
 */

#include "time.h"
#include "cal/cal.h"
#include "arch/m68k/arch.h"

/* Apollo epoch offset (seconds from 1970 to 1980) */
#define APOLLO_EPOCH_OFFSET 0x12CEA600

void TIME_$SET_TIME_OF_DAY(uint32_t *tv, status_$t *status)
{
    uint32_t seconds;
    uint32_t usecs;
    uint32_t unix_secs;
    clock_t new_clock;
    clock_t current_abs;
    clock_t usec_ticks;
    uint16_t saved_sr;

    *status = status_$ok;

    seconds = tv[0];
    usecs = tv[1] & ~0x3;  /* Round to 4-microsecond boundary */

    /* If time is before Apollo epoch, handle specially */
    if ((int32_t)seconds < (int32_t)APOLLO_EPOCH_OFFSET) {
        TIME_$CURRENT_CLOCKH = 0;
        TIME_$CURRENT_CLOCKL = 0;
        TIME_$CURRENT_TIME = seconds;
        TIME_$CURRENT_USEC = usecs;
        return;
    }

    /* Convert Unix time to Apollo time (seconds since 1980) */
    unix_secs = seconds - APOLLO_EPOCH_OFFSET;

    /* Convert seconds to 48-bit clock ticks */
    CAL_$SEC_TO_CLOCK(&unix_secs, &new_clock);

    /* Add microseconds (converted to ticks: usec / 4) */
    usec_ticks.high = 0;
    usec_ticks.low = (uint16_t)(usecs / 4);
    ADD48(&new_clock, &usec_ticks);

    /* Disable interrupts for atomic update */
    GET_SR(saved_sr);
    SET_SR(saved_sr | SR_IPL_DISABLE_ALL);

    /* Adjust boot time if we have a valid current clock */
    if (TIME_$CURRENT_CLOCKH != 0) {
        TIME_$BOOT_TIME = (new_clock.high - TIME_$CURRENT_CLOCKH) + TIME_$BOOT_TIME;
    }

    /* Get current absolute clock */
    TIME_$ABS_CLOCK(&current_abs);

    /* Update current clock values */
    TIME_$CURRENT_CLOCKH = new_clock.high;
    TIME_$CURRENT_TIME = seconds;

    /* Calculate new CLOCKL accounting for elapsed time */
    int32_t diff = (int32_t)new_clock.low - (int32_t)(current_abs.low - TIME_$CLOCKL);
    if (diff < 0) {
        TIME_$CURRENT_CLOCKH--;
    }
    TIME_$CURRENT_CLOCKL = (uint16_t)diff;

    /* Update microseconds accounting for elapsed time */
    TIME_$CURRENT_USEC = usecs - (current_abs.low - TIME_$CLOCKL) * 4;
    if ((int32_t)TIME_$CURRENT_USEC < 0) {
        TIME_$CURRENT_USEC += 1000000;
        TIME_$CURRENT_TIME--;
    }

    /* Enable interrupts */
    SET_SR(saved_sr);

    /* Update hardware RTC */
    /* TODO: Implement CAL_$DECODE_TIME and CAL_$WRITE_CALENDAR calls */
}

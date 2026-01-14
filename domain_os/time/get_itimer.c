/*
 * TIME_$GET_ITIMER - Get interval timer
 *
 * Gets the current value of a real-time or virtual interval timer.
 * This is the Domain/OS implementation of the Unix getitimer() call.
 *
 * Parameters:
 *   which - Pointer to timer type:
 *           0 = ITIMER_REAL
 *           1 = ITIMER_VIRTUAL
 *   value - Receives current timer value
 *   interval - Receives timer interval
 *
 * Original address: 0x00e58f06
 */

#include "time/time_internal.h"

void TIME_$GET_ITIMER(uint16_t *which, uint32_t *value, uint32_t *interval)
{
    clock_t val_clock;
    clock_t int_clock;

    /* Get the raw timer values */
    time_$get_itimer_internal(*which, &val_clock, &int_clock);

    if (*which == 1) {
        /* ITIMER_VIRTUAL - convert from clock ticks to timeval */
        time_$clock_to_itimer(&val_clock, value);
        value[0] = val_clock.high;
        value[1] = val_clock.low;

        time_$clock_to_itimer(&int_clock, interval);
        interval[0] = int_clock.high;
        interval[1] = int_clock.low;
    }
    /* For ITIMER_REAL, values are already in correct format */
}

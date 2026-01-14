/*
 * TIME_$SET_ITIMER - Set interval timer
 *
 * Sets a real-time or virtual interval timer. This is the
 * Domain/OS implementation of the Unix setitimer() call.
 *
 * Parameters:
 *   which - Pointer to timer type:
 *           0 = ITIMER_REAL (real time, delivers SIGALRM)
 *           1 = ITIMER_VIRTUAL (virtual time, delivers SIGVTALRM)
 *   value - New timer value (itimerval: interval + value)
 *   ovalue - Receives old timer value (may be NULL)
 *   status - Status return
 *
 * Original address: 0x00e58e58
 */

#include "time/time_internal.h"

void TIME_$SET_ITIMER(uint16_t *which, uint32_t *value, uint32_t *ovalue,
                      uint32_t *ointerval_ret, uint32_t *oval_ret,
                      status_$t *status)
{
    clock_t val_clock;
    clock_t interval_clock;
    clock_t oval_clock;
    clock_t ointerval_clock;

    /* Register cleanup handler */
    PROC2_$SET_CLEANUP(6);

    if (*which == 0) {
        /* ITIMER_REAL - values are already in ticks */
        time_$set_itimer_internal(0, (clock_t *)value, (clock_t *)ovalue,
                                  (clock_t *)ointerval_ret, (clock_t *)oval_ret,
                                  status);
    } else {
        /* ITIMER_VIRTUAL - convert from timeval to clock ticks */
        time_$itimer_to_clock(&interval_clock, ovalue);
        time_$itimer_to_clock(&val_clock, value);

        time_$set_itimer_internal(1, &val_clock, &interval_clock,
                                  &ointerval_clock, &oval_clock, status);

        /* Convert results back to timeval format */
        time_$clock_to_itimer(&ointerval_clock, ointerval_ret);
        ointerval_ret[0] = ointerval_clock.high;
        ointerval_ret[1] = ointerval_clock.low;

        time_$clock_to_itimer(&oval_clock, oval_ret);
        oval_ret[0] = oval_clock.high;
        oval_ret[1] = oval_clock.low;
    }
}

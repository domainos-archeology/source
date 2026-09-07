/*
 * TIME_$SET_ITIMER - Set interval timer
 *
 * Sets a real-time or virtual interval timer. This is the
 * Domain/OS implementation of the Unix setitimer() call.
 *
 * Parameters:
 *   which     - Pointer to timer type: 0 = real, 1 = virtual
 *   interval  - New reload interval (it_interval), the argument at 0x0C
 *   value     - New time-to-expiry (it_value), the argument at 0x10; zero
 *               disarms the timer (0x00E58DD8 tst.l (A3))
 *   ointerval - Receives the old reload interval (0x14)
 *   ovalue    - Receives the old time-to-expiry (0x18)
 *   status    - Status return
 *
 * Original address: 0x00e58e58
 *
 * Assembly (else branch, `*which != 0`):
 *   00e58ea6  move.l D3,-(SP)            ; source = the caller's `value`  (0x10)
 *   00e58ea8  pea (-0x8,A6)              ; dest   = val_clock
 *   00e58eac  bsr time_$itimer_to_clock
 *   00e58eb6  move.l D2,-(SP)            ; source = the caller's `interval` (0x0C)
 *   00e58eb8  pea (-0x10,A6)             ; dest   = interval_clock
 *   00e58ebc  bsr time_$itimer_to_clock
 *   00e58eca  bsr time_$set_itimer_internal(1, &interval_clock, &val_clock,
 *                                           ointerval, ovalue, status)
 *   00e58ed2  pea (A3)                   ; source = *ointerval (clock form)
 *   00e58ed4  pea (-0x10,A6)             ; dest   = the temp
 *   00e58ed8  bsr time_$clock_to_itimer
 *   00e58ede  move.l (-0x10,A6),(A3) / move.w (-0xc,A6),(0x4,A3)
 *   00e58ee8..00e58ef6  the same for *ovalue
 *
 * Note that the old-value buffers handed to time_$set_itimer_internal are the
 * CALLER's (A3/A2), not locals: the conversion back to itimer form is done in
 * place through the same 6-byte temporary at A6-0x10 that held interval_clock.
 *
 * The two buffer arguments are in `struct itimerval` order - reload interval
 * first, time-to-expiry second; see the note above the prototypes in time.h.
 */

#include "time/time_internal.h"

void TIME_$SET_ITIMER(uint16_t *which, clock_t *interval, clock_t *value,
                      clock_t *ointerval, clock_t *ovalue,
                      status_$t *status)
{
    clock_t val_clock;
    clock_t interval_clock;

    /* 0xE58E7A: Pascal function whose result is discarded (addq.w #4,SP) */
    PROC2_$SET_CLEANUP(6);

    if (*which == 0) {
        /* 0xE58E8A: real timer - the values are already in clock form */
        time_$set_itimer_internal(0, interval, value, ointerval, ovalue,
                                  status);
    } else {
        /* 0xE58E9E: virtual timer - halve the incoming itimer-form values */
        time_$itimer_to_clock(&val_clock, value);
        time_$itimer_to_clock(&interval_clock, interval);

        time_$set_itimer_internal(1, &interval_clock, &val_clock,
                                  ointerval, ovalue, status);

        /*
         * Convert the returned clock values back to itimer form in place.
         * 0xE58ED4 and 0xE58EEA both reuse the A6-0x10 slot, i.e. the
         * temporary that held interval_clock.
         */
        time_$clock_to_itimer(&interval_clock, ointerval);
        ointerval->high = interval_clock.high;
        ointerval->low = interval_clock.low;

        time_$clock_to_itimer(&interval_clock, ovalue);
        ovalue->high = interval_clock.high;
        ovalue->low = interval_clock.low;
    }
}

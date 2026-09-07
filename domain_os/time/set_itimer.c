/*
 * TIME_$SET_ITIMER - Set interval timer
 *
 * Sets a real-time or virtual interval timer. This is the
 * Domain/OS implementation of the Unix setitimer() call.
 *
 * Parameters:
 *   which     - Pointer to timer type: 0 = real, 1 = virtual
 *   value     - New timer value
 *   interval  - New timer interval
 *   ovalue    - Receives the old timer value
 *   ointerval - Receives the old timer interval
 *   status    - Status return
 *
 * Original address: 0x00e58e58
 *
 * Assembly (else branch, `*which != 0`):
 *   00e58ea6  move.l D3,-(SP)            ; source = the caller's `interval`
 *   00e58ea8  pea (-0x8,A6)              ; dest   = interval_clock
 *   00e58eac  bsr time_$itimer_to_clock
 *   00e58eb6  move.l D2,-(SP)            ; source = the caller's `value`
 *   00e58eb8  pea (-0x10,A6)             ; dest   = val_clock
 *   00e58ebc  bsr time_$itimer_to_clock
 *   00e58eca  bsr time_$set_itimer_internal(1, &val_clock, &interval_clock,
 *                                           ovalue, ointerval, status)
 *   00e58ed2  pea (A3)                   ; source = *ovalue (clock form)
 *   00e58ed4  pea (-0x10,A6)             ; dest   = the temp
 *   00e58ed8  bsr time_$clock_to_itimer
 *   00e58ede  move.l (-0x10,A6),(A3) / move.w (-0xc,A6),(0x4,A3)
 *   00e58ee8..00e58ef6  the same for *ointerval
 *
 * Note that the old-value buffers handed to time_$set_itimer_internal are the
 * CALLER's (A3/A2), not locals: the conversion back to itimer form is done in
 * place through the same 6-byte temporary that held val_clock.
 */

#include "time/time_internal.h"

void TIME_$SET_ITIMER(uint16_t *which, clock_t *value, clock_t *interval,
                      clock_t *ovalue, clock_t *ointerval,
                      status_$t *status)
{
    clock_t val_clock;
    clock_t interval_clock;

    /* 0xE58E7A: Pascal function whose result is discarded (addq.w #4,SP) */
    PROC2_$SET_CLEANUP(6);

    if (*which == 0) {
        /* Real timer - the values are already in clock form */
        time_$set_itimer_internal(0, value, interval, ovalue, ointerval,
                                  status);
    } else {
        /* Virtual timer - halve the incoming itimer-form values */
        time_$itimer_to_clock(&interval_clock, interval);
        time_$itimer_to_clock(&val_clock, value);

        time_$set_itimer_internal(1, &val_clock, &interval_clock,
                                  ovalue, ointerval, status);

        /* Convert the returned clock values back to itimer form in place */
        time_$clock_to_itimer(&val_clock, ovalue);
        ovalue->high = val_clock.high;
        ovalue->low = val_clock.low;

        time_$clock_to_itimer(&val_clock, ointerval);
        ointerval->high = val_clock.high;
        ointerval->low = val_clock.low;
    }
}

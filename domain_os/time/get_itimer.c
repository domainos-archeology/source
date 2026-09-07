/*
 * TIME_$GET_ITIMER - Get interval timer
 *
 * Gets the current value of a real-time or virtual interval timer.
 * This is the Domain/OS implementation of the Unix getitimer() call.
 *
 * time_$get_itimer_internal fills the caller's two buffers with clock_t
 * values; for `which == 1` each is then converted in place to the itimer
 * form (a 48-bit left shift by one) through a single 6-byte temporary.
 *
 * Parameters:
 *   which    - Pointer to timer type: 0 = real, 1 = virtual
 *   value    - Receives the current timer value
 *   interval - Receives the timer interval
 *
 * Original address: 0x00e58f06
 *
 * Assembly:
 *   00e58f1a  subq.l #0x2,SP             ; Pascal result slot (discarded)
 *   00e58f1c  pea (A4) / pea (A3) / move.w (A2),-(SP)
 *   00e58f22  bsr time_$get_itimer_internal   ; writes *value and *interval
 *   00e58f2a  cmpi.w #0x1,(A2) / bne -> return
 *   00e58f30  pea (A3)                   ; source  = the caller's buffer
 *   00e58f32  pea (-0x8,A6)              ; dest    = local temp
 *   00e58f36  bsr time_$clock_to_itimer
 *   00e58f3c  move.l (-0x8,A6),(A3)      ; copy the temp back over *value
 *   00e58f40  move.w (-0x4,A6),(0x4,A3)
 *   00e58f46..00e58f54  the same for *interval
 */

#include "time/time_internal.h"

void TIME_$GET_ITIMER(uint16_t *which, clock_t *value, clock_t *interval)
{
    clock_t converted;

    /* 0xE58F22: the caller's buffers are filled directly */
    time_$get_itimer_internal(*which, value, interval);

    if (*which == 1) {
        /* 0xE58F36: dest = &converted, source = *value */
        time_$clock_to_itimer(&converted, value);
        value->high = converted.high;
        value->low = converted.low;

        /* 0xE58F4C */
        time_$clock_to_itimer(&converted, interval);
        interval->high = converted.high;
        interval->low = converted.low;
    }
    /* For which != 1 the values are left in clock form */
}

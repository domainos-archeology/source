/*
 * TIME_$ADVANCE - Schedule a callback after a delay
 *
 * Schedules TIME_$ADVANCE_CALLBACK to be called after the specified
 * delay. Used by TIME_$WAIT to implement timed waits.
 *
 * Parameters:
 *   delay_type - Pointer to delay type (0=relative to current, 1=relative to abs)
 *   delay - Pointer to delay value (clock_t)
 *   ec - Event count to advance when timer fires
 *   elem - Queue element storage (internal use)
 *   status - Status return
 *
 * Original address: 0x00e16454
 *
 * Assembly shows it calls TIME_$ABS_CLOCK to get current time, then
 * TIME_$Q_ADD_CALLBACK with the RTEQ and TIME_$ADVANCE_CALLBACK.
 */

#include "time/time_internal.h"
#include "ec/ec.h"

/* Interval of 0 (one-shot timer) */
static clock_t zero_interval = { 0, 0 };

void TIME_$ADVANCE(uint16_t *delay_type, clock_t *delay, void *ec,
                   void *elem, status_$t *status)
{
    clock_t now;

    /* Get current absolute time */
    TIME_$ABS_CLOCK(&now);

    /* Add callback to RTE queue */
    TIME_$Q_ADD_CALLBACK(&TIME_$RTEQ,
                         delay,                    /* elem handle */
                         *delay_type,              /* relative flag */
                         &now,                     /* current time */
                         TIME_$ADVANCE_CALLBACK,   /* callback */
                         ec,                       /* callback arg */
                         0,                        /* flags */
                         &zero_interval,           /* interval (one-shot) */
                         (time_queue_elem_t *)elem,
                         status);
}

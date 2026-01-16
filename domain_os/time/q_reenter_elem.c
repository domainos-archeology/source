/*
 * TIME_$Q_REENTER_ELEM - Re-enter an element into queue
 *
 * Re-inserts an element that was previously removed back into
 * the queue with a new time. Used for repeating timers.
 *
 * Parameters:
 *   queue - Queue to add to
 *   when - Pointer to clock time when element should fire
 *   flags - Flags (0=add interval, non-0=use when directly)
 *   interval - Pointer to interval to add (if flags==0)
 *   elem - Queue element to re-insert
 *   status - Status return
 *
 * Original address: 0x00e16c8e
 */

#include "time/time_internal.h"

void TIME_$Q_REENTER_ELEM(time_queue_t *queue, clock_t *when, int16_t qflags,
                          clock_t *base_time, time_queue_elem_t *elem, status_$t *status)
{
    uint16_t token;
    status_$t local_status;

    /* Acquire spin lock on queue (lock at tail offset) */
    token = ML_$SPIN_LOCK((uint16_t *)&queue->tail);

    /* Try to remove element first if it's in queue */
    TIME_$Q_REMOVE_ELEM(queue, elem, &local_status);

    /* Check for errors (other than "not in queue") */
    if (local_status != status_$ok &&
        local_status != 0xD000A &&  /* status_$time_queue_elem_not_in_use */
        local_status != 0xD0009) {  /* status_$time_queue_elem_in_use */
        ML_$SPIN_UNLOCK((uint16_t *)&queue->tail, token);
        *status = local_status;
        return;
    }

    /* Copy the 'when' time to elem's expire time */
    elem->expire_high = when->high;
    elem->expire_low = when->low;

    /* If flags == 0, add base_time/interval to expire time */
    if (qflags == 0) {
        clock_t elem_clock;
        elem_clock.high = elem->expire_high;
        elem_clock.low = elem->expire_low;
        ADD48(&elem_clock, base_time);
        elem->expire_high = elem_clock.high;
        elem->expire_low = elem_clock.low;
    }

    /* Re-insert element in sorted order */
    time_$q_insert_sorted(queue, elem);

    /* TODO: Check queue head and potentially trigger timer hardware */

    /* Release spin lock */
    ML_$SPIN_UNLOCK((uint16_t *)&queue->tail, token);
    *status = status_$ok;
}

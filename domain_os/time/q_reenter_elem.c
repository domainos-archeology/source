/*
 * TIME_$Q_REENTER_ELEM - Re-enter an element into queue
 *
 * Re-inserts an element that was previously removed back into
 * the queue. Used for repeating timers.
 *
 * Parameters:
 *   queue - Queue to add to
 *   elem - Queue element to re-insert
 *
 * Original address: 0x00e16c8e
 */

#include "time/time_internal.h"

void TIME_$Q_REENTER_ELEM(time_queue_t *queue, time_queue_elem_t *elem)
{
    uint16_t token;

    /* Acquire spin lock on queue */
    token = ML_$SPIN_LOCK((uint16_t *)&queue->tail);

    /* Re-insert element in sorted order */
    time_$q_insert_sorted(queue, elem);

    /* Release spin lock */
    ML_$SPIN_UNLOCK((uint16_t *)&queue->tail, token);
}

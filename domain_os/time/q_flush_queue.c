/*
 * TIME_$Q_FLUSH_QUEUE - Flush all elements from a queue
 *
 * Removes all elements from a time queue, typically used
 * during shutdown or when canceling all pending timers.
 *
 * Parameters:
 *   queue - Queue to flush
 *
 * Original address: 0x00e16c80
 *
 * This is a simple function that walks the queue and removes
 * all elements.
 */

#include "time/time_internal.h"
#include "ml/ml.h"

void TIME_$Q_FLUSH_QUEUE(time_queue_t *queue)
{
    uint16_t token;
    time_queue_elem_t *elem;
    time_queue_elem_t *next;

    /* Acquire spin lock on queue */
    token = ML_$SPIN_LOCK((uint16_t *)&queue->tail);

    /* Walk through and unlink all elements */
    elem = (time_queue_elem_t *)(uintptr_t)queue->head;
    while (elem != NULL) {
        next = (time_queue_elem_t *)(uintptr_t)elem->next;
        elem->next = 0;
        elem->flags &= ~0x01;  /* Clear in-queue flag */
        elem = next;
    }

    /* Clear queue head */
    queue->head = 0;

    /* Release spin lock */
    ML_$SPIN_UNLOCK((uint16_t *)&queue->tail, token);
}

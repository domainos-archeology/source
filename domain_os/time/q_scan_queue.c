/*
 * TIME_$Q_SCAN_QUEUE - Scan queue and fire expired callbacks
 *
 * Iterates through the queue from head, executing callbacks for
 * all elements whose expiration time has passed.
 *
 * Parameters:
 *   queue - Queue to scan
 *   now - Current time
 *   status - Status return (unused in this implementation)
 *
 * Original address: 0x00e16e94
 *
 * This is a complex function that:
 * 1. Acquires spin lock
 * 2. Loops through queue head while elements are expired
 * 3. For each expired element:
 *    - Removes from queue
 *    - If repeating (flag bit 1), re-enters with new time
 *    - Calls the callback function
 * 4. Releases spin lock
 */

#include "time.h"
#include "ml/ml.h"
#include "cal/cal.h"

/* Internal helper to insert element in sorted order */
extern int8_t time_$q_insert_sorted(time_queue_t *queue, time_queue_elem_t *elem);

/* Internal helper to setup timer if element is at head */
extern void time_$q_setup_timer(time_queue_t *queue, clock_t *when);

/* Queue element flags */
#define QELEM_FLAG_IN_QUEUE  0x01    /* Element is in queue */
#define QELEM_FLAG_REPEAT    0x02    /* Repeating timer */
#define QELEM_FLAG_WIRED     0x04    /* Callback runs wired (in interrupt) */
#define QELEM_FLAG_UNWIRED   0x08    /* Callback runs unwired (deferred) */
#define QELEM_FLAG_ASYNC     0x10    /* Async callback */

void TIME_$Q_SCAN_QUEUE(time_queue_t *queue, clock_t *now, void *status_arg)
{
    uint16_t token;
    time_queue_elem_t *elem;
    clock_t elem_time;
    int cmp_result;

    (void)status_arg;  /* Unused */

scan_again:
    /* Acquire spin lock on queue */
    token = ML_$SPIN_LOCK((uint16_t *)&queue->tail);

    while (queue->head != 0) {
        elem = (time_queue_elem_t *)(uintptr_t)queue->head;

        /* Get element's expiration time */
        elem_time.high = elem->expire_high;
        elem_time.low = elem->expire_low;

        /* Compare with current time */
        /* Using SUB48 to compare: if elem_time - now is negative, elem has expired */
        cmp_result = (int32_t)elem_time.high - (int32_t)now->high;
        if (cmp_result == 0) {
            cmp_result = (int16_t)elem_time.low - (int16_t)now->low;
        }

        /* If element hasn't expired yet, we're done */
        if (cmp_result > 0) {
            break;
        }

        /* Remove element from queue head */
        queue->head = elem->next;
        elem->next = 0;
        elem->flags &= ~QELEM_FLAG_IN_QUEUE;

        /* If repeating timer, schedule next occurrence */
        if (elem->flags & QELEM_FLAG_REPEAT) {
            /* Add interval to expiration time */
            ADD48((clock_t *)&elem->expire_high,
                  (clock_t *)&elem->interval_high);
            /* Re-insert into queue */
            time_$q_insert_sorted(queue, elem);
        }

        /*
         * Execute callback.
         * If wired/unwired flags are set, the callback needs special handling
         * (deferred execution). For now, we just call directly.
         */
        if ((elem->flags & (QELEM_FLAG_WIRED | QELEM_FLAG_UNWIRED)) == 0) {
            /* Direct callback execution */
            void (*callback)(void *) = (void (*)(void *))(uintptr_t)elem->callback;
            void *arg = (void *)(uintptr_t)elem->callback_arg;

            ML_$SPIN_UNLOCK((uint16_t *)&queue->tail, token);
            callback(arg);
            goto scan_again;  /* Re-acquire lock and continue */
        } else {
            /*
             * Deferred callback - would normally be added to DXM queue.
             * For now, just call directly after releasing lock.
             */
            void (*callback)(void *) = (void (*)(void *))(uintptr_t)elem->callback;
            void *arg = (void *)(uintptr_t)elem->callback_arg;

            ML_$SPIN_UNLOCK((uint16_t *)&queue->tail, token);
            callback(arg);
            goto scan_again;
        }
    }

    /* If queue still has elements, set up timer for next one */
    if (queue->head != 0) {
        time_$q_setup_timer(queue, now);
    }

    /* Release spin lock */
    ML_$SPIN_UNLOCK((uint16_t *)&queue->tail, token);
}

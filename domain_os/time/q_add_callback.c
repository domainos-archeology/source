/*
 * TIME_$Q_ADD_CALLBACK - Add a callback to the time queue
 *
 * Populates a queue element with callback information and enters
 * it into the specified time queue.
 *
 * Parameters:
 *   queue - Queue to add to
 *   elem - User's element handle
 *   relative - If 0, 'when' is added to current time; otherwise absolute
 *   when - Pointer to delay/time value
 *   callback - Callback function pointer
 *   callback_arg - Argument for callback
 *   flags - Element flags
 *   interval - Repeat interval (for periodic callbacks)
 *   qelem - Queue element structure to populate
 *   status - Status return
 *
 * Original address: 0x00e16dd4
 *
 * Assembly shows qelem layout:
 *   0x04: callback function
 *   0x08: callback argument
 *   0x0C: expire time high (32 bits)
 *   0x10: expire time low (16 bits)
 *   0x12: flags (16 bits)
 *   0x14: interval high (32 bits)
 *   0x18: interval low (16 bits)
 */

#include "time/time_internal.h"

void TIME_$Q_ADD_CALLBACK(time_queue_t *queue, void *elem, uint16_t relative,
                          clock_t *when, void *callback, void *callback_arg,
                          uint16_t flags, clock_t *interval,
                          time_queue_elem_t *qelem, status_$t *status)
{
    /* Copy expiration time from 'when' */
    qelem->expire_high = when->high;
    qelem->expire_low = when->low;

    /* If relative time, add current time to get absolute expiration */
    if (relative == 0) {
        ADD48((clock_t *)&qelem->expire_high, when);
    }

    /* Set callback info */
    qelem->callback = (uint32_t)(uintptr_t)callback;
    qelem->callback_arg = (uint32_t)(uintptr_t)callback_arg;
    qelem->flags = flags;

    /* Set repeat interval */
    qelem->interval_high = interval->high;
    qelem->interval_low = interval->low;

    /* Enter element into queue */
    TIME_$Q_ENTER_ELEM(queue, when, qelem, status);
}

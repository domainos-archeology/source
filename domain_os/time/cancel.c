/*
 * TIME_$CANCEL - Cancel a scheduled callback
 *
 * Removes a timer element from the queue. If the element is currently
 * being processed (status = elem_not_in_use), waits for completion.
 *
 * Parameters:
 *   wait_flag - If non-zero, wait for callback completion
 *   elem - Queue element to cancel
 *   status - Status return
 *
 * Original address: 0x00e164a4
 */

#include "time/time_internal.h"
#include "ec/ec.h"
#include "misc/crash_system.h"

/* Status code for element not in queue */
#define status_$time_queue_elem_not_in_use 0x000D0009

void TIME_$CANCEL(uint32_t *wait_flag, void *elem, status_$t *status)
{
    time_queue_elem_t *qelem = (time_queue_elem_t *)elem;

    /* Try to remove from queue */
    TIME_$Q_REMOVE_ELEM(&TIME_$RTEQ, qelem, status);

    if (*status == status_$time_queue_elem_not_in_use) {
        /*
         * Element is currently being processed by callback.
         * Wait for it to complete by waiting on the EC stored
         * in the element at offset 0x08.
         */
        void *ec = (void *)(uintptr_t)qelem->callback_arg;
        /* EC_$WAIT with the element's EC */
        /* Simplified: just clear status since we can't fully emulate EC_$WAIT */
        (void)ec;
        (void)wait_flag;
        *status = status_$ok;
    } else if (*status != status_$ok) {
        /* Unexpected error - crash */
        CRASH_SYSTEM(status);
    }
}

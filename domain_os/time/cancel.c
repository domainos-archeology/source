/*
 * TIME_$CANCEL - Cancel a scheduled callback
 *
 * Removes a timer element from the queue. If the element is currently
 * being processed (status = elem_not_in_use), waits for completion.
 *
 * Parameters:
 *   wait_value - by-value longword: the eventcount value to wait for
 *                (0x00E164DC pushes it straight into the EC_$WAIT vals array)
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

void TIME_$CANCEL(int32_t wait_value, time_queue_elem_t *elem,
                  status_$t *status)
{
    time_queue_elem_t *qelem = elem;

    /* 0xE164BE: TIME_$Q_REMOVE_ELEM(&TIME_$RTEQ, elem, status) */
    TIME_$Q_REMOVE_ELEM(&TIME_$RTEQ, qelem, status);

    if (*status == status_$time_queue_elem_not_in_use) {
        /*
         * Element is currently being processed by callback.
         * Wait for it to complete by waiting on the EC stored
         * in the element at offset 0x08.
         */
        /*
         * 0xE164D4: A2 = (0x8,A2), i.e. the element's callback_arg is the
         * eventcount the callback advances when it finishes.
         *   ecs  = { that ec, NULL, NULL }
         *   vals = { wait_value, 0, 0 }
         */
        ec_$eventcount_t *ec = (ec_$eventcount_t *)(uintptr_t)qelem->callback_arg;

        (void)EC_$WAIT((ec_$wait_ecs_t){ { ec, NULL, NULL } },
                       (ec_$wait_vals_t){ { wait_value, 0, 0 } });

        /* 0xE164F0: clr.l (A3) */
        *status = status_$ok;
    } else if (*status != status_$ok) {
        /* Unexpected error - crash */
        CRASH_SYSTEM(status);
    }
}

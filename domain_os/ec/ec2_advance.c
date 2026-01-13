/*
 * EC2_$ADVANCE - Advance a Level 2 Event Count
 *
 * Increments the eventcount and wakes any waiters if present.
 *
 * Parameters:
 *   ec - EC2 pointer
 *   status_ret - Status return
 *
 * Original address: 0x00e42cae
 */

#include "ec.h"

void EC2_$ADVANCE(ec2_$eventcount_t *ec, status_$t *status_ret)
{
    if ((uintptr_t)ec <= 0x3E8) {
        *status_ret = status_$ec2_bad_event_count;
        return;
    }

    *status_ret = status_$ok;

    /* Increment the value */
    ec->value++;

    /* If there are waiters, wake them */
    if (ec->awaiters != 0) {
        EC2_$WAKEUP(ec, status_ret);
    }
}

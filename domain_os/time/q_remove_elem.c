/*
 * TIME_$Q_REMOVE_ELEM - Remove an element from a time queue
 *
 * Frame (0x00E16E4E): 0x08 queue (A2), 0x0C elem, 0x10 status.  Locals:
 * -0x04 the removal status, -0x06 the spin-lock token.
 *
 *   00e16e52  token = ML_$SPIN_LOCK(&queue->lock)
 *   00e16e62  time_$q_remove_internal(queue, elem, &local_status)
 *   00e16e74  ML_$SPIN_UNLOCK(&queue->lock, token)
 *   00e16e84  *status = local_status           ; after the unlock
 *
 * Original address: 0x00e16e48, 76 bytes
 */

#include "time/time_internal.h"

void TIME_$Q_REMOVE_ELEM(time_queue_t *queue, time_queue_elem_t *elem,
                         status_$t *status)
{
    ml_$spin_token_t token;         /* A6-0x06 */
    status_$t local_status;         /* A6-0x04 */

    token = ML_$SPIN_LOCK(&queue->lock);
    time_$q_remove_internal(queue, elem, &local_status);
    ML_$SPIN_UNLOCK(&queue->lock, token);
    *status = local_status;
}

/*
 * TIME_$Q_FLUSH_QUEUE - Discard every element of a time queue
 *
 *   00e16c84  movea.l (0x8,A6),A0
 *   00e16c88  clr.l (A0)              ; head = 0
 *
 * That is the whole routine: no lock is taken, and the elements are not
 * visited - their `next` links and TIME_QELEM_IN_QUEUE bits are left as they
 * were.  Called by PROC1_$UNBIND on the process's VT queue.
 *
 * Original address: 0x00e16c80, 14 bytes
 */

#include "time/time_internal.h"

void TIME_$Q_FLUSH_QUEUE(time_queue_t *queue)
{
    queue->head = 0;
}

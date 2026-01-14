/*
 * TIME_$Q_REMOVE_ELEM - Remove an element from a time queue
 *
 * Removes a queue element from the specified queue.
 * Uses a spin lock to protect queue manipulation.
 *
 * Parameters:
 *   queue - Queue to remove from
 *   elem - Queue element to remove
 *   status - Status return
 *
 * Original address: 0x00e16e48
 *
 * Assembly:
 *   00e16e48    link.w A6,-0x8
 *   00e16e4c    pea (A2)
 *   00e16e4e    movea.l (0x8,A6),A2       ; queue
 *   00e16e52    pea (0x4,A2)              ; &queue->tail (spin lock)
 *   00e16e56    jsr ML_$SPIN_LOCK
 *   00e16e5c    addq.w #0x4,SP
 *   00e16e5e    move.w D0w,(-0x6,A6)      ; save token
 *   00e16e62    pea (-0x4,A6)             ; &local_status
 *   00e16e66    move.l (0xc,A6),-(SP)     ; elem
 *   00e16e6a    pea (A2)                  ; queue
 *   00e16e6c    bsr.w FUN_00e16b70        ; Internal remove
 *   00e16e70    lea (0xc,SP),SP
 *   00e16e74    ; ML_$SPIN_UNLOCK
 *   00e16e84    movea.l (0x10,A6),A0      ; status
 *   00e16e88    move.l (-0x4,A6),(A0)     ; *status = local_status
 */

#include "time/time_internal.h"

void TIME_$Q_REMOVE_ELEM(time_queue_t *queue, time_queue_elem_t *elem,
                         status_$t *status)
{
    uint16_t token;
    status_$t local_status;

    /* Acquire spin lock on queue */
    token = ML_$SPIN_LOCK((uint16_t *)&queue->tail);

    /* Remove element from queue */
    time_$q_remove_internal(queue, elem, &local_status);

    /* Release spin lock */
    ML_$SPIN_UNLOCK((uint16_t *)&queue->tail, token);

    /* Return status */
    *status = local_status;
}

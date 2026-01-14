/*
 * TIME_$Q_INIT_QUEUE - Initialize a time queue
 *
 * Initializes a time queue structure with the given flags and ID.
 *
 * Parameters:
 *   flags - Queue flags (0 = normal, 0xFF = all-queues sentinel)
 *   queue_id - Queue identifier (0 for RTE queue, 1-64 for VT queues)
 *   queue - Pointer to queue structure to initialize
 *
 * Original address: 0x00e16c5e
 *
 * Assembly:
 *   00e16c5e    link.w A6,0x0
 *   00e16c62    move.b (0x8,A6),D0b       ; flags
 *   00e16c66    move.w (0xa,A6),D1w       ; queue_id
 *   00e16c6a    movea.l (0xc,A6),A0       ; queue pointer
 *   00e16c6e    clr.l (A0)                ; head = 0
 *   00e16c70    clr.l (0x4,A0)            ; tail = 0
 *   00e16c74    move.b D0b,(0x8,A0)       ; queue->flags = flags
 *   00e16c78    move.w D1w,(0xa,A0)       ; queue->queue_id = queue_id
 *   00e16c7c    unlk A6
 *   00e16c7e    rts
 */

#include "time.h"

void TIME_$Q_INIT_QUEUE(uint8_t flags, uint16_t queue_id, time_queue_t *queue)
{
    queue->head = 0;
    queue->tail = 0;
    queue->flags = flags;
    queue->queue_id = queue_id;
}

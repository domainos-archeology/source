/*
 * TIME_$Q_INIT_QUEUE - Initialize a time queue header
 *
 *   00e16c62  move.b (0x8,A6),D0b     ; is_vt: high byte of the first word slot
 *   00e16c66  move.w (0xa,A6),D1w     ; queue_id
 *   00e16c6a  movea.l (0xc,A6),A0     ; queue
 *   00e16c6e  clr.l (A0)              ; head
 *   00e16c70  clr.l (0x4,A0)          ; lock
 *   00e16c74  move.b D0b,(0x8,A0)     ; flags
 *   00e16c78  move.w D1w,(0xa,A0)     ; queue_id
 *
 * The pad byte at +0x09 is not written.
 *
 * Original address: 0x00e16c5e, 34 bytes
 */

#include "time/time_internal.h"

void TIME_$Q_INIT_QUEUE(boolean is_vt, uint16_t queue_id, time_queue_t *queue)
{
    queue->head = 0;
    queue->lock = 0;
    queue->flags = is_vt;
    queue->queue_id = queue_id;
}

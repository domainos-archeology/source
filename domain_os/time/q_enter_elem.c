/*
 * TIME_$Q_ENTER_ELEM - Enter an element into a time queue
 *
 * Inserts a queue element in sorted order by expiration time.
 * Uses a spin lock to protect queue manipulation.
 *
 * Parameters:
 *   queue - Queue to add to
 *   when - Expiration time (for insertion sorting)
 *   elem - Queue element to insert
 *   status - Status return
 *
 * Original address: 0x00e16d64
 *
 * Assembly:
 *   00e16d64    link.w A6,-0x4
 *   00e16d68    movem.l {  A3 A2},-(SP)
 *   00e16d6c    movea.l (0x8,A6),A2       ; queue
 *   00e16d70    movea.l (0xc,A6),A3       ; when
 *   00e16d74    movea.l (0x14,A6),A0      ; status
 *   00e16d78    clr.l (A0)                ; *status = 0
 *   00e16d7a    pea (0x4,A2)              ; &queue->tail (spin lock)
 *   00e16d7e    jsr ML_$SPIN_LOCK
 *   00e16d84    addq.w #0x4,SP
 *   00e16d86    move.w D0w,(-0x2,A6)      ; save token
 *   00e16d8a    move.l (0x10,A6),-(SP)    ; elem
 *   00e16d8e    pea (A2)                  ; queue
 *   00e16d90    bsr.w FUN_00e16ae8        ; Insert into sorted position
 *   00e16d94    addq.w #0x8,SP
 *   00e16d96    tst.b D0b                 ; Check if at head
 *   00e16d98    bpl.b skip_timer_setup
 *   ; ... timer setup code if at head ...
 *   00e16dba    ; ML_$SPIN_UNLOCK
 */

#include "time/time_internal.h"

void TIME_$Q_ENTER_ELEM(time_queue_t *queue, clock_t *when,
                        time_queue_elem_t *elem, status_$t *status)
{
    uint16_t token;
    int8_t at_head;

    *status = status_$ok;

    /* Acquire spin lock on queue */
    token = ML_$SPIN_LOCK((uint16_t *)&queue->tail);

    /* Insert element in sorted order by expiration time */
    at_head = time_$q_insert_sorted(queue, elem);

    /*
     * If element was inserted at the head of the queue,
     * we may need to reprogram the timer hardware.
     * Check interrupt flags to see if we're in an interrupt context.
     */
    if (at_head < 0) {  /* Negative means at head */
        if (queue->flags < 0) {  /* flags & 0x80 = VT queue */
            if (IN_VT_INT == 0) {
                time_$q_setup_timer(queue, when);
            }
        } else {
            if (IN_RT_INT == 0) {
                time_$q_setup_timer(queue, when);
            }
        }
    }

    /* Release spin lock */
    ML_$SPIN_UNLOCK((uint16_t *)&queue->tail, token);
}

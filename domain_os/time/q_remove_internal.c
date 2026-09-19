/*
 * time_$q_remove_internal - Unlink an element from a time queue
 *
 * The second unnamed procedure of the TIME_Q_ module, the lock-free body
 * behind TIME_$Q_REMOVE_ELEM (0x00E16E6C) and the removal step of
 * TIME_$Q_REENTER_ELEM (0x00E16CC2); the caller holds the queue's spin lock.
 *
 * Frame (0x00E16B78): 0x08 queue (A2), 0x0C elem, 0x10 status (A1).
 *
 *   00e16b84  btst.b #0x0,(0x13,A0) / bne       ; not queued ->
 *   00e16b8c  move.l #0xd0009,(A1)              ;   status_$time_queue_element_not_in_use
 *   00e16b94  movea.l (A2),A0 / clr.l D0        ; cur = head, prev = nil
 *   00e16b98  cmpa.w #0,A0 / bne                ; end of list ->
 *   00e16b9e  move.l #0xd000a,(A1)              ;   status_$time_queue_element_not_found
 *   00e16ba6  cmpa.l (0xc,A6),A0 / beq          ; found
 *   00e16bac  prev = cur; cur = cur->next; loop
 *   00e16bb2  tst.l D0 / bne                    ; prev == nil ->
 *   00e16bba  move.l (A0),(A2)                  ;   head = elem->next
 *   00e16bc4  move.l (A0),(A3)                  ; else prev->next = elem->next
 *   00e16bc6  clr.l (A0) / bclr.b #0,(0x13,A0) / clr.l (A1)
 *
 * Original address: 0x00e16b70, 106 bytes
 */

#include "time/time_internal.h"

void time_$q_remove_internal(time_queue_t *queue, time_queue_elem_t *elem,
                             status_$t *status)
{
    time_queue_elem_t *cur;         /* A0 */
    time_queue_elem_t *prev;        /* D0 / A3 */

    /* 0x00E16B84 */
    if ((elem->flags & TIME_QELEM_IN_QUEUE) == 0) {
        *status = status_$time_queue_element_not_in_use;
    } else {
        /* 0x00E16B94..0x00E16BB0 */
        cur = (time_queue_elem_t *)ARCH_VA_TO_PTR(queue->head);
        prev = NULL;
        while (cur != elem) {
            if (cur == NULL) {
                break;
            }
            prev = cur;
            cur = (time_queue_elem_t *)ARCH_VA_TO_PTR(cur->next);
        }

        if (cur == NULL) {
            /* 0x00E16B9E */
            *status = status_$time_queue_element_not_found;
        } else {
            /* 0x00E16BB2..0x00E16BCE */
            if (prev == NULL) {
                queue->head = elem->next;
            } else {
                prev->next = elem->next;
            }
            elem->next = 0;
            elem->flags &= (uint16_t)~TIME_QELEM_IN_QUEUE;
            *status = status_$ok;
        }
    }
}

/*
 * time_$q_insert_sorted - Insert an element into a time queue in expiry order
 *
 * The first, unnamed procedure of the TIME_Q_ module (map: I E16AE8 TIME_Q_
 * size 4F8; the first named symbol is TIME_$Q_INIT at 0xE16C5C).  Called by
 * TIME_$Q_REENTER_ELEM (0x00E16D1C), TIME_$Q_ENTER_ELEM (0x00E16D90) and
 * TIME_$Q_SCAN_QUEUE (0x00E16F06); the caller holds the queue's spin lock.
 *
 * Frame (0x00E16AF0): 0x08 queue (A4), 0x0C elem.
 *
 *   00e16af8  btst.b #0x0,(0x13,A0) / beq       ; already queued?
 *   00e16b00  pea (0x6a,PC) -> 0x00E16B6C       ; status_$time_queue_element_already_in_use
 *   00e16b04  jsr CRASH_SYSTEM
 *   00e16b0c  move.l (A4),D2 / suba.l A2,A2     ; cur = head, prev = nil
 *   00e16b10  movea.l D2,A0 / cmpa.w #0,A0 / beq ; end of list -> insert
 *   00e16b1a  copy cur->expire to (-0xc,A6)
 *   00e16b2a  SUB48(&copy, &elem->expire)
 *   00e16b3a  tst.b D0b / bmi                   ; cur.expire - elem.expire >= 0 -> insert here
 *   00e16b3e  prev = cur; cur = cur->next; loop
 *   00e16b44  elem->next = cur; bset.b #0,(0x13,A0)
 *   00e16b50  cmpa.w #0,A2 / seq D0b            ; result: true when prev == nil
 *   00e16b56  tst.b D0b / bpl                   ; head = elem : prev->next = elem
 *
 * Returns the Domain boolean left in D0: true (0xFF) when the element became
 * the new head of the queue.
 *
 * Original address: 0x00e16ae8, 130 bytes
 */

#include "time/time_internal.h"
#include "misc/misc.h"

/*
 * Constant cell at 0x00E16B6C (`gsk read`: 00 0d 00 0d), between this
 * procedure's rts (0x00E16B68) and time_$q_remove_internal (0x00E16B70);
 * passed by reference to CRASH_SYSTEM.
 */
static const status_$t time_$c_already_in_use =
    status_$time_queue_element_already_in_use;

int8_t time_$q_insert_sorted(time_queue_t *queue, time_queue_elem_t *elem)
{
    uint32_t cur;                   /* D2: VA of the node being compared */
    time_queue_elem_t *cur_p;       /* A3 */
    time_queue_elem_t *prev;        /* A2 */
    clock_t cur_expire;             /* A6-0x0C */
    int8_t at_head;                 /* D0 */

    /* 0x00E16AF8..0x00E16B0A */
    if ((elem->flags & TIME_QELEM_IN_QUEUE) != 0) {
        CRASH_SYSTEM(&time_$c_already_in_use);
    }

    /* 0x00E16B0C..0x00E16B42: walk until a node not earlier than elem */
    cur = queue->head;
    prev = NULL;
    for (;;) {
        cur_p = (time_queue_elem_t *)ARCH_VA_TO_PTR(cur);
        if (cur_p == NULL) {
            break;
        }
        cur_expire.high = cur_p->expire_high;
        cur_expire.low = cur_p->expire_low;
        /* SUB48 is true (negative) when cur_expire - elem->expire >= 0 */
        if (SUB48(&cur_expire, (clock_t *)&elem->expire_high) < 0) {
            break;
        }
        prev = cur_p;
        cur = cur_p->next;
    }

    /* 0x00E16B44..0x00E16B5E */
    elem->next = cur;
    elem->flags |= TIME_QELEM_IN_QUEUE;
    at_head = (prev == NULL) ? true : false;
    if (at_head < 0) {
        queue->head = ARCH_PTR_TO_VA(elem);
    } else {
        prev->next = ARCH_PTR_TO_VA(elem);
    }
    return at_head;
}

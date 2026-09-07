/*
 * TIME_$Q_REENTER_ELEM - Re-enter an element into queue
 *
 * Re-inserts an element that was previously removed back into
 * the queue with a new time. Used for repeating timers.
 *
 * Parameters:
 *   queue - Queue to add to
 *   when - Pointer to clock time when element should fire
 *   flags - Flags (0=add interval, non-0=use when directly)
 *   interval - Pointer to interval to add (if flags==0)
 *   elem - Queue element to re-insert
 *   status - Status return
 *
 * Original address: 0x00e16c8e
 *
 * Assembly:
 *   00e16c96  movea.l (0x8,A6),A3       ; queue
 *   00e16c9a  move.w (0x10,A6),D2w      ; qflags
 *   00e16c9e  movea.l (0x12,A6),A4      ; base_time
 *   00e16ca2  movea.l (0x16,A6),A2      ; elem
 *   00e16ca6  move.l (0x1a,A6),D3       ; status
 *   00e16caa  pea (0x4,A3) / jsr ML_$SPIN_LOCK
 *   00e16cc2  bsr time_$q_remove_internal(queue, elem, &local_status)
 *   00e16cca  the three statuses that are treated as success
 *   00e16cf8  move.l (A0),(0xc,A2) / move.w (0x4,A0),(0x10,A2)
 *   00e16d06  tst.w D2w / bne
 *   00e16d10  jsr ADD48(&elem->expire, base_time)
 *   00e16d1c  bsr time_$q_insert_sorted
 *   00e16d22  tst.b D0b / bpl           ; only a new head re-arms the timer
 *   00e16d26  tst.b (0x8,A3) / bpl      ; queue->flags picks which flag
 *   00e16d2c  tst.b IN_VT_INT  /  00e16d34  tst.b IN_RT_INT
 *   00e16d3a  bmi                       ; already inside the handler
 *   00e16d40  bsr time_$q_setup_timer(queue, base_time)
 *   00e16d58  clr.l (A0)                ; *status = status_$ok
 *
 * The removal at 0x00E16CC2 is time_$q_remove_internal, the lock-free form -
 * the lock is already held here.
 */

#include "time/time_internal.h"

void TIME_$Q_REENTER_ELEM(time_queue_t *queue, clock_t *when, int16_t qflags,
                          clock_t *base_time, time_queue_elem_t *elem, status_$t *status)
{
    uint16_t token;
    status_$t local_status;

    /* Acquire spin lock on queue (lock at tail offset) */
    token = ML_$SPIN_LOCK((uint16_t *)&queue->tail);

    /* 0xE16CC2: the internal, lock-free removal */
    time_$q_remove_internal(queue, elem, &local_status);

    /* Check for errors (other than "not in queue") */
    if (local_status != status_$ok &&
        local_status != status_$time_queue_element_not_found &&
        local_status != status_$time_queue_element_not_in_use) {
        ML_$SPIN_UNLOCK((uint16_t *)&queue->tail, token);
        *status = local_status;
        return;
    }

    /* 0xE16CF8 */
    elem->expire_high = when->high;
    elem->expire_low = when->low;

    /* 0xE16D06: a zero flag means 'when' is relative to *base_time */
    if (qflags == 0) {
        ADD48((clock_t *)&elem->expire_high, base_time);
    }

    /* 0xE16D1C: re-insert in sorted order; true means it became the head */
    if (time_$q_insert_sorted(queue, elem) < 0) {
        /*
         * 0xE16D26 - 0xE16D3A.  A new head only re-arms the hardware when we
         * are not already running inside that queue's interrupt handler: a
         * negative queue->flags selects the virtual timer (IN_VT_INT) and a
         * non-negative one the real-time timer (IN_RT_INT).  The handler will
         * program the timer itself on its way out.
         */
        int8_t in_int = ((int8_t)queue->flags < 0) ? (int8_t)IN_VT_INT
                                                   : (int8_t)IN_RT_INT;

        if (in_int >= 0) {
            time_$q_setup_timer(queue, base_time);      /* 0xE16D40 */
        }
    }

    /* 0xE16D46 */
    ML_$SPIN_UNLOCK((uint16_t *)&queue->tail, token);
    *status = status_$ok;                               /* 0xE16D58 */
}

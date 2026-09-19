/*
 * TIME_$Q_REENTER_ELEM - Move an element to a new expiry time
 *
 * Frame (0x00E16C96): 0x08 queue (A3), 0x0C when, 0x10 is_absolute (word,
 * D2), 0x12 now (A4), 0x16 elem (A2), 0x1A status (D3).  Locals: -0x04 the
 * removal status, -0x06 the spin-lock token.
 *
 *   00e16caa  token = ML_$SPIN_LOCK(&queue->lock)
 *   00e16cba  time_$q_remove_internal(queue, elem, &local_status)
 *   00e16cca  local_status == 0, == 0xD000A or == 0xD0009 -> continue
 *   00e16ce0  else ML_$SPIN_UNLOCK; *status = local_status
 *   00e16cf8  elem->expire = *when
 *   00e16d06  tst.w D2w / bne                  ; relative -> ADD48(&expire, now)
 *   00e16d18  time_$q_insert_sorted(queue, elem); tst.b D0b / bpl
 *   00e16d26  tst.b (0x8,A3) / bpl             ; VT queue: IN_VT_INT, else IN_RT_INT
 *   00e16d3a  bmi                              ; inside that handler -> skip
 *   00e16d3c  time_$q_setup_timer(queue, now)
 *   00e16d46  ML_$SPIN_UNLOCK; *status = 0
 *
 * Original address: 0x00e16c8e, 214 bytes
 */

#include "time/time_internal.h"

void TIME_$Q_REENTER_ELEM(time_queue_t *queue, clock_t *when,
                          int16_t is_absolute, clock_t *now,
                          time_queue_elem_t *elem, status_$t *status)
{
    ml_$spin_token_t token;         /* A6-0x06 */
    status_$t local_status;         /* A6-0x04 */
    int8_t in_int;

    token = ML_$SPIN_LOCK(&queue->lock);

    /* 0x00E16CC2: the lock-free removal; "not queued" outcomes are fine */
    time_$q_remove_internal(queue, elem, &local_status);

    if (local_status != status_$ok &&
        local_status != status_$time_queue_element_not_found &&
        local_status != status_$time_queue_element_not_in_use) {
        /* 0x00E16CE0 */
        ML_$SPIN_UNLOCK(&queue->lock, token);
        *status = local_status;
    } else {
        /* 0x00E16CF8 */
        elem->expire_high = when->high;
        elem->expire_low = when->low;

        /* 0x00E16D06: zero means `when` is relative to *now */
        if (is_absolute == 0) {
            ADD48((clock_t *)&elem->expire_high, now);
        }

        /* 0x00E16D1C */
        if (time_$q_insert_sorted(queue, elem) < 0) {
            /*
             * 0x00E16D26..0x00E16D3A: a new head re-arms the timer unless
             * that queue's own interrupt handler is running (it programs
             * the timer itself on the way out).
             */
            in_int = (queue->flags < 0) ? (int8_t)IN_VT_INT : (int8_t)IN_RT_INT;
            if (in_int >= 0) {
                time_$q_setup_timer(queue, now);        /* 0x00E16D40 */
            }
        }

        /* 0x00E16D46 */
        ML_$SPIN_UNLOCK(&queue->lock, token);
        *status = status_$ok;                           /* 0x00E16D58 */
    }
}

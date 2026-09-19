/*
 * TIME_$Q_ENTER_ELEM - Insert a prepared element into a time queue
 *
 * Frame (0x00E16D6C): 0x08 queue (A2), 0x0C now (A3), 0x10 elem, 0x14 status.
 *
 *   00e16d74  *status = 0                      ; before the lock
 *   00e16d7a  token = ML_$SPIN_LOCK(&queue->lock)
 *   00e16d8a  time_$q_insert_sorted(queue, elem); tst.b D0b / bpl
 *   00e16d9a  tst.b (0x8,A2) / bpl             ; VT queue: IN_VT_INT, else IN_RT_INT
 *   00e16dae  bmi                              ; inside that handler -> skip
 *   00e16db0  time_$q_setup_timer(queue, now)
 *   00e16dba  ML_$SPIN_UNLOCK
 *
 * Original address: 0x00e16d64, 112 bytes
 */

#include "time/time_internal.h"

void TIME_$Q_ENTER_ELEM(time_queue_t *queue, clock_t *now,
                        time_queue_elem_t *elem, status_$t *status)
{
    ml_$spin_token_t token;         /* A6-0x02 */
    int8_t in_int;

    *status = status_$ok;                                   /* 0x00E16D78 */

    token = ML_$SPIN_LOCK(&queue->lock);

    /* 0x00E16D90: true (negative) when elem became the head */
    if (time_$q_insert_sorted(queue, elem) < 0) {
        in_int = (queue->flags < 0) ? (int8_t)IN_VT_INT : (int8_t)IN_RT_INT;
        if (in_int >= 0) {
            time_$q_setup_timer(queue, now);                /* 0x00E16DB4 */
        }
    }

    ML_$SPIN_UNLOCK(&queue->lock, token);                   /* 0x00E16DC4 */
}

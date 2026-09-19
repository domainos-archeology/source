/*
 * TIME_$Q_SCAN_QUEUE - Fire every expired element at the head of a queue
 *
 * Called from the timer interrupt paths with the current clock.  Under the
 * queue's spin lock it pops elements whose expiry is at or before `now`:
 * each is unlinked, re-inserted at expiry + interval when it repeats, and
 * then either handed to a DXM queue (flags bit 2 -> DXM_$UNWIRED_Q, bit 3 ->
 * DXM_$WIRED_Q; the lock is dropped around that and re-taken afterwards) or
 * called directly WITH THE LOCK STILL HELD.  When the scan stops at an
 * element that has not expired, the hardware timer is re-armed for it.
 *
 * Parameters (frame 0x00E16E9C..0x00E16EA4):
 *   0x08 queue  - A3
 *   0x0C now    - D3 (clock_t *)
 *   0x10 status - A4; only ever handed on to DXM_$ADD_CALLBACK as its
 *                 status_ret (`pea (A4)` at 0x00E16F42 / 0x00E16F64).  The
 *                 direct path never writes it.
 *
 * Original address: 0x00e16e94, 330 bytes
 *
 * Frame locals:
 *   -0x1C  data_cell   longword: &elem->callback_arg, passed BY ADDRESS to
 *                      DXM_$ADD_CALLBACK as its `data` (4 bytes copied)
 *   -0x14  check_dup   Domain boolean = flags bit 4 (`sne D0b`, 0x00E16F12)
 *   -0x12  token       ML_$SPIN_LOCK's result word
 *   -0x10  expiry      6-byte copy of the element's expiry
 *   -0x08  elem_cell   longword holding the element's address, passed BY
 *                      ADDRESS to a direct callback (0x00E16FA0..0x00E16FAC)
 *
 * Constant queue addresses: 0xE2ADC4 = DXM_$UNWIRED_Q (0x00E16F5C),
 * 0xE2ADE0 = DXM_$WIRED_Q (0x00E16F7E).
 */

#include "time/time_internal.h"
#include "dxm/dxm.h"

void TIME_$Q_SCAN_QUEUE(time_queue_t *queue, clock_t *now, status_$t *status)
{
    ml_$spin_token_t token;         /* A6-0x12 */
    time_queue_elem_t *elem;        /* A2 (D2 holds the same VA) */
    clock_t expiry;                 /* A6-0x10 */
    boolean check_dup;              /* A6-0x14 */
    void *data_cell;                /* A6-0x1C */
    time_queue_elem_t *elem_cell;   /* A6-0x08 */
    dxm_queue_t *dxm_queue;
    int8_t not_expired;             /* D0b from SUB48 */
    uint16_t flags;

    /* 0x00E16EA8: bra.w 0x00e16f8e - take the lock, then test the head */
    token = ML_$SPIN_LOCK(&queue->lock);            /* 0x00E16F8E..0x00E16F9A */

    /* 0x00E16FB0: tst.l (A3) / bne.w 0x00e16eac */
    while (queue->head != 0) {
        /* 0x00E16EAC..0x00E16EB6 */
        elem = (time_queue_elem_t *)ARCH_VA_TO_PTR(queue->head);
        expiry.high = elem->expire_high;
        expiry.low = elem->expire_low;

        /*
         * 0x00E16EBC..0x00E16ECA: three-word `cmpm.w (A1)+,(A0)+ / dbne`
         * over {expiry, *now}; equal -> expired (0x00E16EE0).  Otherwise
         * SUB48(&expiry, now) at 0x00E16ED2: it returns 0xFF when the
         * difference is non-negative, i.e. the head is still in the future,
         * and `bmi.w 0x00e16fb6` then ends the scan.
         */
        if (!(expiry.high == now->high && expiry.low == now->low)) {
            not_expired = SUB48(&expiry, now);
            if (not_expired < 0) {
                break;
            }
        }

        /* 0x00E16EE0..0x00E16EE4: unlink the head and clear its in-queue bit */
        queue->head = elem->next;
        elem->next = 0;
        elem->flags = (uint16_t)(elem->flags & ~TIME_QELEM_IN_QUEUE);

        /* 0x00E16EEA: btst.b #0x1,(0x13,A2) - repeating element */
        if ((elem->flags & TIME_QELEM_REPEAT) != 0) {
            /* 0x00E16EF2..0x00E16F0A: expiry += interval; re-insert (result ignored) */
            ADD48((clock_t *)&elem->expire_high, (clock_t *)&elem->interval_high);
            (void)time_$q_insert_sorted(queue, elem);
        }

        /* 0x00E16F0C..0x00E16F14: check_dup = (flags bit 4 set) as 0xFF / 0 */
        flags = elem->flags;
        check_dup = (boolean)(((flags & TIME_QELEM_CHECK_DUP) != 0) ? 0xFF : 0);

        /* 0x00E16F18..0x00E16F26: bit 2 or bit 3 -> deferred; neither -> direct */
        if ((flags & TIME_QELEM_UNWIRED) != 0 || (flags & TIME_QELEM_WIRED) != 0) {
            /* 0x00E16F28..0x00E16F38: release the queue lock first */
            ML_$SPIN_UNLOCK(&queue->lock, token);

            /*
             * 0x00E16F3A..0x00E16F8A: DXM_$ADD_CALLBACK(dxm_queue,
             * &elem->callback, &data_cell, 4, check_dup, status) where
             * data_cell = &elem->callback_arg (`lea (0x8,A2),A0 / move.l
             * A0,(-0x1c,A6)`), so 4 bytes of callback_arg are copied into
             * the DXM entry.  Bit 2 wins when both are set.
             */
            if ((flags & TIME_QELEM_UNWIRED) != 0) {
                dxm_queue = &DXM_$UNWIRED_Q;                /* 0x00E16F5C */
            } else {
                dxm_queue = &DXM_$WIRED_Q;                  /* 0x00E16F7E */
            }
            data_cell = &elem->callback_arg;
            DXM_$ADD_CALLBACK(dxm_queue, &elem->callback, &data_cell, 4,
                              check_dup, status);

            /* 0x00E16F8E..0x00E16F9E: re-take the lock, then re-test the head */
            token = ML_$SPIN_LOCK(&queue->lock);
        } else {
            /*
             * 0x00E16FA0..0x00E16FAE: the callback is handed the ADDRESS of
             * a cell holding the element (see time_$callback_arg_t), and
             * runs with the queue lock still held.
             */
            elem_cell = elem;
            dxm_$callback_fn(elem->callback)(&elem_cell);
        }
    }

    /* 0x00E16FB6..0x00E16FC2: re-arm the timer for the new head, if any */
    if (queue->head != 0) {
        time_$q_setup_timer(queue, now);
    }

    /* 0x00E16FC4..0x00E16FCE: ML_$SPIN_UNLOCK(&queue->lock, token); its
     * argument bytes are reclaimed by unlk. */
    ML_$SPIN_UNLOCK(&queue->lock, token);
}

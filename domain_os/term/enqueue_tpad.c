#include "term/term_internal.h"

/*
 * TERM_$ENQUEUE_TPAD - DXM callback that drains the tablet-pad sample queue
 * (0x00E72472..0x00E724C2, 80 bytes; map: TERM_$ENQUEUE_TPAD at E72472).
 *
 * Reached through the DXM queue: KBD_$RCV (0x00E1CDF8) queues the ADDRESS of
 * the cell PTR_TERM_$ENQUEUE_TPAD_00e1ce90 with a 4-byte datum, and DXM hands
 * this routine a pointer to that datum.  The datum is itself the address of
 * a cell holding the queue's address (SUMA_$STATE.tpad_buffer, which
 * SUMA_$INIT points at TERM_$TPAD_BUFFER), hence the double indirection at
 * entry:
 *   0x00E7247A  movea.l (0x8,A6),A0   ; A0 = param1
 *   0x00E7247E  movea.l (A0),A1       ; A1 = *param1 (the datum)
 *   0x00E72480  move.l (A1),D0        ; queue base
 *   0x00E72484  lea (0x2,A3),A2       ; A2 = &queue->tail
 *
 * Loop 0x00E7248C..0x00E724B8: while queue->head (re-read from (A3) on
 * every pass, 0x00E724B4) != queue->tail:
 *   TPAD_$DATA(&queue->samples[tail])            ; base + 4 + tail*16
 *   queue->tail = M$OIS$WLW(tail + 1, 6)         ; wrap at 6 entries
 * M$OIS$WLW's result slot is a word (subq.l #2,SP, 0x00E7249E) and the
 * dividend is the sign-extended tail plus one (ext.l / addq.l #1).
 *
 * The queue is a tpad_buffer_t (head, tail, six 16-byte samples); the
 * 16-byte suma_sample_t slot is what TPAD_$DATA reads as a
 * tpad_$data_packet_t.
 */
void TERM_$ENQUEUE_TPAD(void **param1) {
    tpad_buffer_t *queue;
    short tail;

    queue = *(tpad_buffer_t **)*param1;                     /* 0x00E7247A..0x00E72482 */

    /* 0x00E72488 bra.b to the test; 0x00E724B4 re-reads head each pass */
    while (queue->head != queue->tail) {
        tail = (short)queue->tail;                          /* 0x00E7248C */
        TPAD_$DATA((tpad_$data_packet_t *)&queue->samples[tail]); /* 0x00E72490..0x00E72494 */

        tail = (short)queue->tail;                          /* 0x00E7249C */
        tail = M$OIS$WLW((long)tail + 1, 6);                /* 0x00E7249E..0x00E724AA */
        queue->tail = (uint16_t)tail;                       /* 0x00E724B2 */
    }
}

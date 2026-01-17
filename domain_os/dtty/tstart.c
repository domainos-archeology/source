/*
 * dtty/tstart.c - DTTY_$TSTART implementation
 *
 * Terminal start function that outputs buffered data and calls a callback.
 *
 * Original address: 0x00E1D6D0
 */

#include "dtty/dtty_internal.h"

/*
 * DTTY_$TSTART - Terminal start with callback
 *
 * Outputs buffered string data and then calls a callback function.
 * This is used for deferred output handling in the terminal subsystem.
 *
 * The buffer uses a circular buffer scheme:
 * - 'current' tracks the output position
 * - 'target' indicates how far to output
 * - 'end' marks the end of valid data in the buffer
 *
 * If current > target, it means we've wrapped around:
 * 1. Output from current to end (wrap case)
 * 2. Reset current to 1
 * Then if current < target:
 * 3. Output from current to target
 * 4. Update current to target
 *
 * Buffer data starts at offset 6 (after the three uint16_t fields),
 * and uses 1-based indexing. So buffer data for position N is at
 * buffer + 5 + N (the +5 = 6 - 1 for 1-based adjustment).
 *
 * Assembly (0x00E1D6D0):
 *   link.w  A6,#-0x8
 *   movem.l {A3 A2},-(SP)
 *   movea.l (0x8,A6),A2           ; A2 = tstart param
 *   movea.l (0x8,A2),A3           ; A3 = buffer_info
 *   move.w  (A3),D0w              ; D0 = current
 *   cmp.w   (0x2,A3),D0w          ; Compare with target
 *   bls.b   no_wrap               ; If current <= target, no wrap
 *   ; Wrap case: output from current to end
 *   move.w  (0x4,A3),D1w          ; D1 = end
 *   addq.w  #1,D1w                ; D1 = end + 1
 *   sub.w   D0w,D1w               ; D1 = (end + 1) - current = length
 *   move.w  D1w,(-0x6,A6)         ; local_a[0] = length
 *   pea     (-0x6,A6)             ; push &length
 *   pea     (0x5,A3,D0w*1)        ; push &data[current] (buffer + current + 5)
 *   bsr     DTTY_$WRITE_STRING
 *   addq.w  #8,SP
 *   move.w  #1,(A3)               ; current = 1
 * no_wrap:
 *   move.w  (A3),D0w              ; D0 = current (may have been reset)
 *   cmp.w   (0x2,A3),D0w          ; Compare with target
 *   bcc.b   done_output           ; If current >= target, done
 *   ; Output from current to target
 *   move.w  (0x2,A3),D1w          ; D1 = target
 *   sub.w   D0w,D1w               ; D1 = target - current = length
 *   move.w  D1w,(-0x6,A6)         ; local_a[0] = length
 *   pea     (-0x6,A6)             ; push &length
 *   pea     (0x5,A3,D0w*1)        ; push &data[current]
 *   bsr.w   DTTY_$WRITE_STRING
 *   addq.w  #8,SP
 *   move.w  (0x2,A3),(A3)         ; current = target
 * done_output:
 *   move.l  (0x4,A2),-(SP)        ; Push callback_arg
 *   movea.l (A2),A0               ; A0 = callback
 *   jsr     (A0)                  ; Call callback(callback_arg)
 *   movem.l (-0x10,A6),{A2 A3}
 *   unlk    A6
 *   rts
 */
void DTTY_$TSTART(dtty_tstart_t *tstart)
{
    dtty_buffer_t *buf;
    uint16_t current;
    int16_t length;

    buf = (dtty_buffer_t *)tstart->buffer_info;
    current = buf->current;

    /* Check for wrap-around case: current > target means we need to
     * output from current to end first, then wrap */
    if (buf->target < current) {
        /* Output from current position to end of buffer */
        length = (buf->end + 1) - current;
        DTTY_$WRITE_STRING((char *)buf + 5 + (int16_t)current, &length);

        /* Reset current to beginning */
        buf->current = 1;
    }

    /* Reload current in case it was reset */
    current = buf->current;

    /* Output from current to target if there's more data */
    if (current < buf->target) {
        length = buf->target - current;
        DTTY_$WRITE_STRING((char *)buf + 5 + (int16_t)current, &length);

        /* Update current to target */
        buf->current = buf->target;
    }

    /* Call the callback function */
    tstart->callback(tstart->callback_arg);
}

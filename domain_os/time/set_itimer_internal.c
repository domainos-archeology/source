/*
 * time_$set_itimer_internal - arm or disarm a process's interval timer
 *
 * Replaces TIME_$ITIMER_DB[which][PROC1_$AS_ID].  The previous setting is
 * returned first (through time_$get_itimer_internal), the element is taken
 * off whichever queue it was on, and - unless the new value is zero - it is
 * re-armed with TIME_$Q_ADD_CALLBACK against the real-time queue (which == 0)
 * or the calling process's virtual-time queue.
 *
 * Original address: 0x00e58d14 (324 bytes)
 *
 * Assembly:
 *   00e58d14  link.w A6,-0x18
 *   00e58d1c  move.w (0x8,A6),D2w        ; which
 *   00e58d20  move.l (0xa,A6),D3         ; interval
 *   00e58d24  movea.l (0xe,A6),A3        ; value
 *   00e58d28  movea.l (0x1a,A6),A2       ; status
 *   00e58d2c  clr.l (A2)                 ; *status = status_$ok
 *   00e58d2e  tst.w D2w / bne 0x00e58d4c
 *   00e58d32  move.l #0xe58a38,(-0xc,A6) ; TIME_$SET_ITIMER_REAL_CALLBACK
 *   00e58d3a  move.l #0xe2a7a0,D4        ; &TIME_$RTEQ
 *   00e58d40  pea (-0x14,A6) / jsr TIME_$ABS_CLOCK
 *   00e58d4c  move.l #0xe58a98,(-0xc,A6) ; TIME_$SET_ITIMER_VIRT_CALLBACK
 *   00e58d54..00e58d6c                   ; D4 = 0xE2A4A0 - 0xC + current*0xC
 *   00e58d6e  pea (-0x14,A6) / jsr PROC1_$GET_CPUT8
 *   00e58d7a  move.w #-0x8000,D0w
 *   00e58d7e  cmp.w (A3),D0w  / bls 0x00e58d88   ; value    >= 2^47 -> error
 *   00e58d82  movea.l D3,A0
 *   00e58d84  cmp.w (A0),D0w  / bhi 0x00e58d92   ; interval <  2^47 -> ok
 *   00e58d88  move.l #0xd000e,(A2)               ; relative time is too large
 *   00e58d92  subq.l #0x2,SP                     ; unused Pascal result slot
 *   00e58d94  push (0x16,A6), (0x12,A6), D2w
 *   00e58d9e  bsr time_$get_itimer_internal(which, ointerval, ovalue)
 *   00e58da6..00e58dc8                   ; A4 = base for `which`, entry = +as_id
 *   00e58dcc  move.l D4,-(SP) / jsr TIME_$Q_REMOVE_ELEM(queue, entry, status)
 *   00e58dd8  tst.l (A3) / bne / tst.w (0x4,A3) / bne 0x00e58e00
 *   00e58df2  move.l (A3),(0xc,A4,D1w)   ; entry->expire = *value (== 0)
 *   00e58df6  move.w (0x4,A3),(0x10,A4,D1w)
 *   00e58dfc  clr.l (A2)                 ; *status = status_$ok, done
 *   00e58e00  movea.l D3,A0
 *   00e58e02  tst.l (A0) / bne / tst.w (0x4,A0) / beq 0x00e58e10
 *   00e58e0c  moveq #0x12,D2             ; repeating
 *   00e58e10  clr.w D2w                  ; one-shot
 *   00e58e12..00e58e46                   ; ten arguments, right to left
 *   00e58e48  jsr TIME_$Q_ADD_CALLBACK
 *
 * Notes:
 *   - "cmp.w (A3),D0w" with D0w = 0x8000 reads only the TOP WORD of the
 *     48-bit value's high longword, so the limit is 2^47 ticks.
 *   - D2 holds `which` up to 0x00E58E0C, where it is reused for the repeat
 *     flag; `which` has no further use by then.
 *   - the callback argument is the address-space id, zero-extended to a
 *     longword ("clr.l D5 / move.w PROC1_$AS_ID,D5w" at 0x00E58E30); the two
 *     callbacks read it back as a word at +2 (0x00E58A44).
 *   - the caller's two-byte result slot at 0x1E is never written.
 */

#include "time/time_internal.h"

void time_$set_itimer_internal(uint16_t which, clock_t *interval,
                               clock_t *value, clock_t *ointerval,
                               clock_t *ovalue, status_$t *status)
{
    time_queue_elem_t *entry;
    time_queue_t *queue;
    void *callback;
    clock_t now;
    uint16_t repeat_flag;

    /* 0xE58D2C */
    *status = status_$ok;

    if (which == 0) {
        /* 0xE58D32 */
        callback = (void *)TIME_$SET_ITIMER_REAL_CALLBACK;
        queue = &TIME_$RTEQ;
        TIME_$ABS_CLOCK(&now);
    } else {
        /* 0xE58D4C; the VT queue table is 1-based on PROC1_$CURRENT */
        callback = (void *)TIME_$SET_ITIMER_VIRT_CALLBACK;
        queue = &TIME_$VTQ[PROC1_$CURRENT - 1];
        PROC1_$GET_CPUT8(&now);
    }

    /*
     * 0xE58D7A: both 48-bit arguments must be below 2^47.  Only the top word
     * of each high longword is compared, which is what the shift expresses
     * without assuming a byte order.
     */
    if ((uint16_t)(value->high >> 16) >= 0x8000 ||
        (uint16_t)(interval->high >> 16) >= 0x8000) {
        *status = status_$time_relative_time_is_too_large;
        return;
    }

    /* 0xE58D9E: hand back the previous setting before disturbing it */
    time_$get_itimer_internal(which, ointerval, ovalue);

    /* 0xE58DA8..0xE58DC8 */
    entry = time_$itimer_entry(which, PROC1_$AS_ID);

    /* 0xE58DCE */
    TIME_$Q_REMOVE_ELEM(queue, entry, status);

    /* 0xE58DD8: a zero it_value disarms the timer */
    if (value->high == 0 && value->low == 0) {
        /* 0xE58DF2: the (zero) words are stored back over the expiry */
        entry->expire_high = value->high;
        entry->expire_low = value->low;
        *status = status_$ok;
        return;
    }

    /* 0xE58E02: a non-zero reload interval makes the element repeating */
    if (interval->high != 0 || interval->low != 0) {
        repeat_flag = ITIMER_FLAG_REPEATING;
    } else {
        repeat_flag = 0;
    }

    /*
     * 0xE58E12..0xE58E48.  is_absolute is the literal zero pushed at
     * 0xE58E42, so the queue adds `now` to `value` to form the deadline.
     */
    TIME_$Q_ADD_CALLBACK(queue,
                         value,
                         0,
                         &now,
                         callback,
                         (void *)(uintptr_t)PROC1_$AS_ID,
                         (uint16_t)(ITIMER_FLAG_BASE | repeat_flag),
                         interval,
                         entry,
                         status);
}

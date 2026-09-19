/*
 * TIME_$ADVANCE - Schedule an eventcount advance on the real-time queue
 *
 * Reads the absolute clock and enters `elem` into TIME_$RTEQ so that
 * TIME_$ADVANCE_CALLBACK advances `ec` when `when` (relative to now, or
 * absolute when *is_absolute is non-zero) is reached.  Used by TIME_$WAIT /
 * TIME_$WAIT2 to implement timed waits.
 *
 * Parameters (frame 0x00E16488..0x00E1646C):
 *   0x08 is_absolute - by reference; its WORD is pushed by value to
 *                      TIME_$Q_ADD_CALLBACK (0x00E1648C move.w (A0),-(SP))
 *   0x0C when        - clock_t, the element's expiry
 *   0x10 ec          - stored as the element's callback_arg (0x00E1647A)
 *   0x14 elem        - the queue element
 *   0x18 status      - status return
 *
 * Original address: 0x00e16454, 80 bytes
 *
 * A5 = 0xE29198, the TIME_ data segment (map: D E29198 TIME_ size 1628):
 *   (0x1608,A5) = 0xE2A7A0  TIME_$RTEQ            (0x00E16492)
 *   (0x1614,A5) = 0xE2A7AC  time_$zero_interval   (0x00E16474), the shared
 *                           all-zero one-shot interval cell - NOT a local copy
 */

#include "time/time_internal.h"

void TIME_$ADVANCE(uint16_t *is_absolute, clock_t *when, ec_$eventcount_t *ec,
                   time_queue_elem_t *elem, status_$t *status)
{
    clock_t now;    /* A6-0x8 */

    /* 0x00E16460..0x00E1646A: pea (-0x8,A6) / jsr TIME_$ABS_CLOCK */
    TIME_$ABS_CLOCK(&now);

    /*
     * 0x00E1646C..0x00E16496, pushes right to left:
     *   (0x18,A6) status, (0x14,A6) elem, pea (0x1614,A5) interval,
     *   clr.w flags, (0x10,A6) ec as callback_arg, #0xe16434 callback,
     *   pea (-0x8,A6) now, move.w (A0) *is_absolute, (0xc,A6) when,
     *   pea (0x1608,A5) queue.
     */
    TIME_$Q_ADD_CALLBACK(&TIME_$RTEQ,
                         when,
                         *is_absolute,
                         &now,
                         (void *)TIME_$ADVANCE_CALLBACK,
                         (void *)ec,
                         0,
                         &time_$zero_interval,
                         elem,
                         status);

    /* 0x00E1649C: movea.l (-0xc,A6),A5 - the caller's A5 is restored */
}

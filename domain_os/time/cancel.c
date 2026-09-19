/*
 * TIME_$CANCEL - Cancel a scheduled real-time queue element
 *
 * Removes `elem` from TIME_$RTEQ.  If the removal reports that the element
 * is no longer queued (its callback is already running or has run), the
 * caller is instead parked on the eventcount the element carries until it
 * reaches `wait_value`; any other non-zero status crashes the system.
 *
 * Parameters (frame 0x00E164B2..0x00E164DC):
 *   0x08 wait_value - BY VALUE longword (0x00E164DC move.l (0x8,A6),-(SP)):
 *                     the value handed to EC_$WAIT.  Callers push it with
 *                     `pea (0x1).w`, i.e. the constant 1.
 *   0x0C elem       - the queue element (A2)
 *   0x10 status     - status return (A3)
 *
 * Original address: 0x00e164a4, 102 bytes (the batch file names 0x00E164DC,
 * which is the EC_$WAIT argument push inside this function)
 *
 * A5 = 0xE29198 (TIME_ data segment); (0x1608,A5) = 0xE2A7A0 = TIME_$RTEQ.
 */

#include "time/time_internal.h"
#include "misc/crash_system.h"

void TIME_$CANCEL(int32_t wait_value, time_queue_elem_t *elem,
                  status_$t *status)
{
    ec_$eventcount_t *ec;

    /* 0x00E164BA..0x00E164C8: TIME_$Q_REMOVE_ELEM(&TIME_$RTEQ, elem, status) */
    TIME_$Q_REMOVE_ELEM(&TIME_$RTEQ, elem, status);

    /* 0x00E164CC: cmpi.l #0xd0009,(A3) / bne.b 0x00e164f4 */
    if (*status == status_$time_queue_element_not_in_use) {
        /*
         * 0x00E164D4: movea.l (0x8,A2),A2 - the element's callback_arg is
         * the eventcount its callback advances (TIME_$ADVANCE stores the
         * caller's ec there).
         *
         * 0x00E164D8..0x00E164EA, pushes right to left: clr.l (val3),
         * clr.l (val2), (0x8,A6) (val1), #0 (ec3), move.l (SP),-(SP) (ec2
         * = the same 0), pea (A2) (ec1); jsr EC_$WAIT.  The result in D0 is
         * ignored and the 24 bytes are reclaimed by unlk.
         */
        ec = (ec_$eventcount_t *)ARCH_VA_TO_PTR(elem->callback_arg);
        (void)EC_$WAIT((ec_$wait_ecs_t){ { ec, NULL, NULL } },
                       (ec_$wait_vals_t){ { wait_value, 0, 0 } });

        /* 0x00E164F0: clr.l (A3) */
        *status = status_$ok;
    } else if (*status != status_$ok) {
        /* 0x00E164F4..0x00E164FA: tst.l (A3) / beq.b / pea (A3) / jsr CRASH_SYSTEM */
        CRASH_SYSTEM(status);
    }

    /* 0x00E16500: movem.l (-0x10,A6),{A2 A3 A5} / unlk / rts */
}

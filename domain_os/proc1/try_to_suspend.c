/*
 * PROC1_$TRY_TO_SUSPEND - Internal: attempt to suspend a process
 *
 * Attempts to actually suspend a process. If the process is currently
 * inhibited, the suspend is deferred by setting a flag.
 *
 * When suspension succeeds:
 * - Removes process from ready list (if not waiting)
 * - Sets suspended flag, clears deferred flag
 * - Advances the suspend eventcount
 *
 * Parameters:
 *   pcb - Process to suspend
 *
 * Original address: 0x00e1471c
 */

#include "proc1/proc1_internal.h"
#include "ec/ec.h"

void PROC1_$TRY_TO_SUSPEND(proc1_t *pcb)
{
    int8_t inhibit_result;

    /* Set deferred suspend flag */
    pcb->pri_max |= PROC1_FLAG_DEFER_SUSP;

    /* Check if process is inhibited (returns -1 if inhibited, 0 if not) */
    inhibit_result = PROC1_$INHIBIT_CHECK(pcb);

    if (inhibit_result >= 0) {
        /* Process is not inhibited, can suspend now */

        /* If not waiting on EC, remove from ready list */
        if ((pcb->pri_max & PROC1_FLAG_WAITING) == 0) {
            PROC1_$REMOVE_READY(pcb);
        }

        /* Set suspended flag, clear deferred flag */
        pcb->pri_min = (pcb->pri_min & ~PROC1_FLAG_DEFER_SUSP) | PROC1_FLAG_SUSPENDED;

        /* Notify waiters that a process was suspended */
        EC_$ADVANCE(&PROC1_$SUSPEND_EC);
    }
    /* If inhibited, the deferred flag remains set and
     * suspension will happen when the inhibit ends */
}

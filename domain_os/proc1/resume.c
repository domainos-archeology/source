/*
 * PROC1_$RESUME - Resume a suspended process
 *
 * Resumes a process that was previously suspended. If the process
 * has a deferred suspend pending (but wasn't fully suspended),
 * just clears the deferred flag.
 *
 * Parameters:
 *   pid - Process ID to resume (1-64)
 *   status_p - Status return
 *
 * Status codes:
 *   status_$illegal_process_id - Invalid PID (0 or > 64)
 *   status_$process_not_bound - Process slot not in use
 *   status_$process_not_suspended - Process was not suspended
 *
 * Original address: 0x00e1476e
 */

#include "proc1.h"

void PROC1_$RESUME(uint16_t pid, status_$t *status_p)
{
    proc1_t *pcb;
    uint8_t flags;

    /* Validate process ID */
    if (pid == 0 || pid > 0x40) {
        *status_p = status_$illegal_process_id;
        return;
    }

    /* Get PCB pointer */
    pcb = PCBS[pid];
    flags = pcb->pri_max;

    /* Check if process is bound (in use) */
    if ((flags & PROC1_FLAG_BOUND) == 0) {
        *status_p = status_$process_not_bound;
        return;
    }

    *status_p = status_$ok;

    /* TODO: Need to disable interrupts here (ori #0x700,SR) */

    /* Check if actually suspended */
    if ((flags & PROC1_FLAG_SUSPENDED) != 0) {
        /* Clear suspended flag */
        pcb->pri_max &= ~PROC1_FLAG_SUSPENDED;

        /* If not waiting on EC, add back to ready list */
        if ((pcb->pri_max & PROC1_FLAG_WAITING) == 0) {
            PROC1_$ADD_READY(pcb);
        }

        PROC1_$DISPATCH();
        return;
    }

    /* Check if deferred suspend was pending */
    if ((flags & PROC1_FLAG_DEFER_SUSP) != 0) {
        /* Clear deferred suspend flag */
        pcb->pri_max &= ~PROC1_FLAG_DEFER_SUSP;
        /* TODO: Need to clear interrupts here (andi #-0x701,SR) */
        return;
    }

    /* Process wasn't suspended */
    *status_p = status_$process_not_suspended;
    /* TODO: Need to clear interrupts here (andi #-0x701,SR) */
}

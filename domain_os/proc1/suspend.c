/*
 * PROC1_$SUSPEND - Suspend a process
 *
 * Attempts to suspend the specified process. If the process is currently
 * running or inhibited, sets a deferred suspend flag.
 *
 * Parameters:
 *   process_id - Process ID to suspend (1-64)
 *   status_ret - Status return
 *
 * Returns:
 *   -1 (0xFF) if process was already suspended or is now suspended
 *   0 if suspension was deferred
 *
 * Status codes:
 *   status_$illegal_process_id - Invalid PID (0 or > 64)
 *   status_$process_not_bound - Process slot not in use
 *   status_$process_already_suspended - Process was already suspended
 *
 * Original address: 0x00e147fa
 */

#include "proc1.h"

int8_t PROC1_$SUSPEND(uint16_t process_id, status_$t *status_ret)
{
    proc1_t *pcb;
    uint8_t flags;
    int8_t result = -1;

    /* Validate process ID */
    if (process_id == 0 || process_id > 0x40) {
        *status_ret = status_$illegal_process_id;
        return result;
    }

    /* Get PCB pointer */
    pcb = PCBS[process_id];
    flags = pcb->pri_max;

    /* Check if process is bound (in use) */
    if ((flags & PROC1_FLAG_BOUND) == 0) {
        *status_ret = status_$process_not_bound;
        return result;
    }

    /* Check if already suspended or deferred */
    if ((flags & (PROC1_FLAG_SUSPENDED | PROC1_FLAG_DEFER_SUSP)) != 0) {
        /* Return whether it's currently suspended (bit 1) */
        result = (flags & PROC1_FLAG_SUSPENDED) ? -1 : 0;
        *status_ret = status_$process_already_suspended;
        return result;
    }

    /* Try to suspend the process */
    /* TODO: Need to disable interrupts here (ori #0x700,SR) */
    PROC1_$TRY_TO_SUSPEND(pcb);
    PROC1_$DISPATCH();

    /* Check if suspension succeeded */
    result = (pcb->pri_max & PROC1_FLAG_SUSPENDED) ? -1 : 0;
    *status_ret = status_$ok;

    return result;
}

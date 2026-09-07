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

#include "proc1/proc1_internal.h"

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

    /*
     * Try to suspend the process.
     *
     * 0x00E14850: ori #0x700,SR -- no SR is saved.  The epilogue at
     * 0x00E1486C does NOT lower the IPL again; PROC1_$DISPATCH
     * (0x00E20A18, called at 0x00E1485C) is what does.
     */
    SET_IPL7();

    /* 0x00E14854: pea (A3) / bsr.w 0x00E1471C */
    PROC1_$TRY_TO_SUSPEND(pcb);

    /* 0x00E1485C: jsr 0x00E20A18 */
    PROC1_$DISPATCH();

    /*
     * 0x00E14862: btst.b #0x1,(0x55,A3) / sne D0b -- the flag byte is
     * re-read after the dispatch, not taken from the cached copy.
     */
    result = (pcb->pri_max & PROC1_FLAG_SUSPENDED) ? -1 : 0;

    /* 0x00E1486A: clr.l (A2) */
    *status_ret = status_$ok;

    return result;
}

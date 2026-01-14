/*
 * PROC1_$SUSPENDP - Check if process is suspended (predicate)
 * Original address: 0x00e14876
 *
 * This function checks if a process is suspended. Unlike PROC1_$SUSPEND,
 * this is a predicate that returns the suspend state without modifying it.
 *
 * Parameters:
 *   pid - Process ID to check
 *   status_ret - Pointer to status return
 *
 * Returns:
 *   -1 (true) if process is suspended
 *   0 (false) if process is not suspended
 */

#include "proc1.h"

int8_t PROC1_$SUSPENDP(uint16_t pid, status_$t *status_ret)
{
    proc1_t *pcb;
    int8_t result = -1;  /* Default to true */

    /* Validate PID: must be non-zero and <= 0x40 (64) */
    if (pid == 0 || pid > 0x40) {
        *status_ret = status_$illegal_process_id;
        return result;
    }

    /* Get PCB from table */
    pcb = PCBS[pid];

    /* Check if process is bound (bit 3 = 0x08 at offset 0x55) */
    /* Note: Ghidra shows 0x800 because it's checking the word at 0x54-0x55 */
    if ((pcb->pri_max & PROC1_FLAG_BOUND) == 0) {
        *status_ret = status_$process_not_bound;
        return result;
    }

    /* Return whether process is suspended (bit 1 = 0x02)
     * Note: Ghidra shows 0x200 because it's checking the word at 0x54-0x55
     * The sne instruction sets D0 to -1 if ZF=0, 0 if ZF=1
     */
    result = (pcb->pri_max & PROC1_FLAG_SUSPENDED) ? -1 : 0;
    *status_ret = status_$ok;

    return result;
}

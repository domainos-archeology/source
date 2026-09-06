/*
 * PROC2_$COMPLETE_FORK - Complete fork in child process
 *
 * Called by the child process after a fork to signal completion.
 * Advances the eventcount that the parent is waiting on.
 *
 * Parameters:
 *   status_ret - Pointer to receive status (unused in practice)
 *
 * Original address: 0x00e735f8
 */

#include "proc2/proc2_internal.h"

/*
 * Per-process eventcount table PROC2_$EC (proc2_internal.h).
 * Each process has an entry with two eventcounts (24 bytes per entry),
 * indexed by process table index (1-based).  The fork completion
 * eventcount is the first one in each entry:
 *   pea (-0x18,A1,D0*1) with A1 = 0xE2B978, D0 = index * 0x18
 */

void PROC2_$COMPLETE_FORK(status_$t *status_ret)
{
    int16_t current_idx;
    void *ec;

    /* Get current process's table index */
    current_idx = P2_PID_TO_INDEX(PROC1_$CURRENT);

    /* Calculate eventcount address:
     * EC table has 24-byte entries indexed by process table index;
     * entry (index - 1) holds this process's fork completion EC.
     */
    ec = PROC_FORK_EC(current_idx);

    /* Advance the eventcount to signal fork completion */
    EC_$ADVANCE(ec);
}

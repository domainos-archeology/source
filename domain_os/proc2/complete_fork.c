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

#include "proc2.h"
#include "../ec/ec.h"

/*
 * Per-process eventcount table.
 * Each process has an entry with multiple eventcounts (24 bytes per entry).
 * Indexed by process table index (1-based).
 * The fork completion eventcount is the first one in each entry.
 *
 * Layout: Base + (index - 1) * 24
 * - Offset 0x00 (EC_TABLE_BASE - 0x18): Fork completion EC
 * - Offset 0x0C: Creation record EC
 */
#if defined(M68K)
    #define PROC_EC_TABLE_BASE  0xE2B978
    #define PROC_FORK_EC(idx)   ((ec_$eventcount_t*)(PROC_EC_TABLE_BASE + ((idx) - 1) * 24 - 24))
#else
    extern ec_$eventcount_t *proc_ec_table;
    #define PROC_FORK_EC(idx)   ((ec_$eventcount_t*)((char*)proc_ec_table + ((idx) - 1) * 24 - 24))
#endif

void PROC2_$COMPLETE_FORK(status_$t *status_ret)
{
    int16_t current_idx;
    void *ec;

    /* Get current process's table index */
    current_idx = P2_PID_TO_INDEX(PROC1_$CURRENT);

    /* Calculate eventcount address:
     * EC table has 24-byte entries indexed by process table index.
     * The fork completion eventcount is at offset -24 (before entry start).
     */
    ec = PROC_FORK_EC(current_idx);

    /* Advance the eventcount to signal fork completion */
    EC_$ADVANCE(ec);
}

/*
 * PROC2_$GET_DEBUGGER_PID - Get debugger's PROC1 PID
 *
 * Returns the PROC1 PID of the process debugging the current process.
 * If the current process is not being debugged, returns 0.
 *
 * Returns: PROC1 PID of debugger, or 0 if not being debugged
 *
 * Original address: 0x00e41b72
 */

#include "proc2/proc2_internal.h"

uint16_t PROC2_$GET_DEBUGGER_PID(void)
{
    int16_t my_index;
    int16_t debugger_idx;
    proc2_info_t *entry;
    proc2_info_t *debugger_entry;

    /* Get my proc2 index from PID mapping table */
    my_index = P2_PID_TO_INDEX(PROC1_$CURRENT);

    entry = P2_INFO_ENTRY(my_index);

    /* Get debugger index */
    debugger_idx = entry->debugger_idx;

    if (debugger_idx == 0) {
        return 0;  /* Not being debugged */
    }

    /* Look up debugger's PROC1 PID */
    debugger_entry = P2_INFO_ENTRY(debugger_idx);
    return debugger_entry->level1_pid;
}

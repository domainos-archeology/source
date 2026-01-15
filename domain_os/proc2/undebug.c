/*
 * PROC2_$UNDEBUG - Stop debugging a process
 *
 * Stops debugging the specified process. The caller must be the
 * current debugger of the target process.
 *
 * Parameters:
 *   proc_uid   - UID of process to stop debugging
 *   status_ret - Pointer to receive status
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$proc2_uid_not_found - Target process not found
 *   status_$proc2_proc_not_debug_target - Caller is not debugger of target
 *
 * Original address: 0x00e41810
 */

#include "proc2/proc2_internal.h"

/* Status codes */
#define status_$proc2_proc_not_debug_target     0x00190010

void PROC2_$UNDEBUG(uid_t *proc_uid, status_$t *status_ret)
{
    uid_t uid;
    status_$t status;
    int16_t proc_idx;
    int16_t current_idx;
    proc2_info_t *entry;

    status = status_$ok;
    uid.high = proc_uid->high;
    uid.low = proc_uid->low;

    ML_$LOCK(PROC2_LOCK_ID);

    /* Find the target process */
    proc_idx = PROC2_$FIND_INDEX(&uid, &status);

    if (status == status_$ok) {
        entry = P2_INFO_ENTRY(proc_idx);

        /* Get current process index */
        current_idx = P2_PID_TO_INDEX(PROC1_$CURRENT);

        /* Verify caller is the debugger of this process */
        /* debugger_idx is at offset 0x5E in entry (relative to table calc) */
        if (entry->debugger_idx == current_idx) {
            /* Clear debug state */
            DEBUG_CLEAR_INTERNAL(proc_idx, 0xFF);
        } else {
            status = status_$proc2_proc_not_debug_target;
        }
    }

    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status;
}

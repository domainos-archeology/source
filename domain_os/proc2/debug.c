/*
 * PROC2_$DEBUG - Start debugging a process
 *
 * Attaches the calling process as the debugger of the target process.
 * If proc_uid is UID_$NIL, debugs the current process's parent.
 *
 * Parameters:
 *   proc_uid   - UID of process to debug (or UID_$NIL for parent)
 *   status_ret - Pointer to receive status
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$proc2_uid_not_found - Target process not found
 *   status_$proc2_process_already_debugging - Target already has debugger
 *   status_$proc2_permission_denied - ACL check failed
 *
 * Original address: 0x00e41620
 */

#include "proc2.h"

/* External globals */
extern uid_$t UID_$NIL;

/* ACL permission check for debug rights */
extern int8_t ACL_$CHECK_DEBUG_RIGHTS(uint16_t *pid);

/* Internal helper to set up debug relationship */
extern void DEBUG_SETUP_INTERNAL(int16_t target_idx, int16_t debugger_idx, int8_t flag);

void PROC2_$DEBUG(uid_$t *proc_uid, status_$t *status_ret)
{
    uid_$t uid;
    status_$t status;
    int16_t target_idx;
    int16_t debugger_idx;
    int8_t flag;
    proc2_info_t *entry;

    status = status_$ok;
    uid.high = proc_uid->high;
    uid.low = proc_uid->low;

    ML_$LOCK(PROC2_LOCK_ID);

    if (uid.high == UID_$NIL.high && uid.low == UID_$NIL.low) {
        /*
         * Debug current process's parent.
         * Get parent index from current process's entry.
         */
        int16_t current_idx = P2_PID_TO_INDEX(PROC1_$CURRENT);
        proc2_info_t *current_entry = P2_INFO_ENTRY(current_idx);

        /* Parent index is stored in first_debug_target_idx field of current process's parent */
        /* Actually, look up parent's proc2 index from PID mapping */
        target_idx = current_entry->first_debug_target_idx;  /* Parent idx stored here */

        /* Get the parent entry to get its parent_idx as debugger_idx */
        proc2_info_t *parent_entry = P2_INFO_ENTRY(target_idx);
        debugger_idx = parent_entry->first_debug_target_idx;
        flag = 0;
    } else {
        /* Find target process by UID */
        target_idx = PROC2_$FIND_INDEX(&uid, &status);

        if (status != status_$ok) {
            goto done;
        }

        entry = P2_INFO_ENTRY(target_idx);

        /* Check if process is already being debugged */
        if (entry->debugger_idx != 0) {
            status = status_$proc2_process_already_debugging;
            goto done;
        }

        /* Check ACL debug permissions */
        /* ACL_$CHECK_DEBUG_RIGHTS returns negative on success */
        if (ACL_$CHECK_DEBUG_RIGHTS(&PROC1_$CURRENT) >= 0) {
            status = status_$proc2_permission_denied;
            goto done;
        }

        /* Get current process's PROC2 index as debugger */
        debugger_idx = P2_PID_TO_INDEX(PROC1_$CURRENT);
        flag = 0xFF;
    }

    /* Set up debug relationship */
    DEBUG_SETUP_INTERNAL(target_idx, debugger_idx, flag);

done:
    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status;
}

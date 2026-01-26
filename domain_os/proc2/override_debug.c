/*
 * PROC2_$OVERRIDE_DEBUG - Override debug settings
 *
 * Attaches the calling process as the debugger of the target process,
 * overriding any existing debug relationship. Unlike DEBUG, this does
 * NOT check if the target is already being debugged.
 *
 * If proc_uid is UID_$NIL, debugs the current process's parent.
 *
 * Parameters:
 *   proc_uid   - UID of process to debug (or UID_$NIL for parent)
 *   status_ret - Pointer to receive status
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$proc2_uid_not_found - Target process not found
 *   status_$proc2_permission_denied - ACL check failed
 *
 * Original address: 0x00e41722
 */

#include "proc2/proc2_internal.h"

void PROC2_$OVERRIDE_DEBUG(uid_t *proc_uid, status_$t *status_ret)
{
    uid_t uid;
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

        /* Parent index is stored in first_debug_target_idx field */
        target_idx = current_entry->first_debug_target_idx;

        /* Get the parent entry */
        proc2_info_t *parent_entry = P2_INFO_ENTRY(target_idx);
        debugger_idx = parent_entry->first_debug_target_idx;
        flag = 0;
    } else {
        /* Find target process by UID */
        target_idx = PROC2_$FIND_INDEX(&uid, &status);

        if (status != status_$ok) {
            goto done;
        }

        /* NOTE: Unlike DEBUG, we do NOT check if already being debugged */
        entry = P2_INFO_ENTRY(target_idx);

        /* Check ACL debug permissions */
        /* ACL_$CHECK_DEBUG_RIGHTS returns negative on success */
        /* TODO: Verify the second parameter - should be pointer to target's PID */
        if (ACL_$CHECK_DEBUG_RIGHTS(&PROC1_$CURRENT, (int16_t *)entry) >= 0) {
            status = status_$proc2_permission_denied;
            goto done;
        }

        /* Get current process's PROC2 index as debugger */
        debugger_idx = P2_PID_TO_INDEX(PROC1_$CURRENT);
        flag = 0xFF;
    }

    /* Set up debug relationship (will unlink from old debugger if needed) */
    DEBUG_SETUP_INTERNAL(target_idx, debugger_idx, flag);

done:
    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status;
}

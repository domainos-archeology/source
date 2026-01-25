/*
 * PROC2_$SET_PGROUP - Set process group for a process
 *
 * Sets the process group for a target process. The caller must have
 * permission to modify the target's process group:
 * - Same session as target, OR
 * - Parent of target (and target not orphaned, or target is being debugged)
 *
 * The new process group must be in the same session as the target.
 *
 * Parameters:
 *   proc_uid   - UID of target process
 *   new_upgid  - Pointer to new process group ID (0 to leave pgroup)
 *   status_ret - Pointer to receive status
 *
 * Status codes:
 *   status_$ok                         - Success
 *   status_$proc2_uid_not_found        - Target process not found
 *   status_$proc2_permission_denied    - Caller lacks permission
 *   status_$proc2_pgroup_in_different_session - New pgroup not in same session
 *
 * Original address: 0x00e410c8
 */

#include "proc2/proc2_internal.h"

void PROC2_$SET_PGROUP(uid_t *proc_uid, uint16_t *new_upgid, status_$t *status_ret)
{
    uid_t local_uid;
    uint16_t upgid;
    int16_t target_idx;
    int16_t current_idx;
    proc2_info_t *target_entry;
    proc2_info_t *current_entry;
    pgroup_entry_t *pgroup;
    int16_t pgroup_idx;
    status_$t status;

    /* Copy UID to local storage */
    local_uid.high = proc_uid->high;
    local_uid.low = proc_uid->low;
    upgid = *new_upgid;

    ML_$LOCK(PROC2_LOCK_ID);

    /* Find the target process */
    target_idx = PROC2_$FIND_INDEX(&local_uid, &status);
    if (status != status_$ok) {
        goto done;
    }

    /* Get current process entry */
    current_idx = P2_PID_TO_INDEX(PROC1_$CURRENT);
    current_entry = P2_INFO_ENTRY(current_idx);
    target_entry = P2_INFO_ENTRY(target_idx);

    /*
     * Permission check:
     * - If caller and target have same owner_session, proceed
     * - Else if caller's owner_session == target's parent_pgroup_idx, check flags
     */
    if (current_entry->owner_session == target_entry->owner_session) {
        /* Same session - permission granted */
        goto permission_ok;
    }

    if (current_entry->owner_session != target_entry->parent_pgroup_idx) {
        /* Caller is not parent */
        status = status_$proc2_uid_not_found;
        goto done;
    }

    /* Caller is parent - check orphan/debug flags */
    if ((target_entry->flags & PROC2_FLAG_ORPHAN) != 0 &&
        (target_entry->flags & PROC2_FLAG_DEBUG) == 0) {
        /* Target is orphaned and not being debugged */
        status = status_$proc2_permission_denied;
        goto done;
    }

    /* Check that target's session matches caller's session */
    if (target_entry->session_id != current_entry->session_id) {
        status = status_$proc2_pgroup_in_different_session;
        goto done;
    }

permission_ok:
    /* Validate new pgroup if non-zero */
    if (upgid != 0) {
        /* Check if target's session_id equals its upid (session leader can't change pgroup) */
        if (target_entry->session_id == target_entry->upid) {
            status = status_$proc2_pgroup_in_different_session;
            goto done;
        }

        /* If new UPGID differs from target's UPID, verify it's in same session */
        if (upgid != 0 && upgid != target_entry->upid) {
            pgroup_idx = PGROUP_FIND_BY_UPGID(upgid);
            pgroup = PGROUP_ENTRY(pgroup_idx);

            /* Pgroup must exist and be in same session */
            if (pgroup_idx == 0 ||
                (uint16_t)pgroup->session_id != current_entry->session_id) {
                status = status_$proc2_pgroup_in_different_session;
                goto done;
            }
        }
    }

    /* Actually set the pgroup */
    PGROUP_SET_INTERNAL(target_entry, upgid, &status);

    /* If setting pgroup to 0, also clear session_id */
    if (upgid == 0) {
        target_entry->session_id = 0;
    }

done:
    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status;
}

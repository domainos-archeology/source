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

/*
 * Internal helper: search pgroup table for entry with matching UPGID.
 * Returns index (1-69) if found, 0 if not found.
 * Original address: 0x00e42224
 */
int16_t PGROUP_FIND_BY_UPGID(uint16_t upgid)
{
    int16_t i;

    for (i = 1; i < PGROUP_TABLE_SIZE; i++) {
        pgroup_entry_t *entry = PGROUP_ENTRY(i);
        if (entry->ref_count == 0) {
            continue;
        }
        if (entry->upgid == upgid) {
            return i;
        }
    }
    return 0;
}

/*
 * Internal helper: set process's pgroup.
 * Handles pgroup table allocation, reference counting, and leader tracking.
 * Original address: 0x00e41e86
 */
void PGROUP_SET_INTERNAL(proc2_info_t *entry, uint16_t new_upgid, status_$t *status)
{
    int16_t new_pgroup_idx;
    int16_t old_pgroup_idx;
    pgroup_entry_t *pgroup;
    int16_t i;

    *status = status_$ok;

    if (new_upgid == 0) {
        /* Leaving pgroup - just clear the index */
        /* Note: full cleanup would call FUN_00e420b8(entry, 2) here */
        entry->pgroup_table_idx = 0;
        return;
    }

    /* Look for existing pgroup with this UPGID */
    new_pgroup_idx = PGROUP_FIND_BY_UPGID(new_upgid);

    if (new_pgroup_idx == 0) {
        /* Need to allocate a new pgroup slot */
        for (i = 1; i < PGROUP_TABLE_SIZE; i++) {
            pgroup = PGROUP_ENTRY(i);
            if (pgroup->ref_count == 0) {
                break;
            }
        }

        if (i >= PGROUP_TABLE_SIZE) {
            /* No free slots - system crash in original code */
            /* For now, return an error */
            *status = status_$proc2_bad_process_group;
            entry->pgroup_table_idx = 0;
            return;
        }

        /* Initialize new pgroup entry */
        new_pgroup_idx = i;
        pgroup = PGROUP_ENTRY(new_pgroup_idx);
        pgroup->ref_count = 1;
        pgroup->leader_count = 0;
        pgroup->upgid = new_upgid;
        pgroup->session_id = entry->session_id;
    } else {
        /* Existing pgroup - verify session matches */
        pgroup = PGROUP_ENTRY(new_pgroup_idx);
        if ((int16_t)entry->session_id != (int16_t)pgroup->session_id) {
            *status = status_$proc2_pgroup_in_different_session;
            return;
        }
        /* Increment reference count */
        pgroup->ref_count++;
    }

    /* Decrement old pgroup's reference count if we had one */
    old_pgroup_idx = entry->pgroup_table_idx;
    if (old_pgroup_idx != 0) {
        pgroup_entry_t *old_pgroup = PGROUP_ENTRY(old_pgroup_idx);
        old_pgroup->ref_count--;
    }

    /* Update leader counts for parent process if applicable */
    if (entry->parent_pgroup_idx != 0) {
        proc2_info_t *parent = P2_INFO_ENTRY(entry->parent_pgroup_idx);
        if (entry->session_id == parent->session_id) {
            int16_t parent_pgroup = parent->pgroup_table_idx;

            /* Decrement old leader count if different from old pgroup */
            if (old_pgroup_idx != 0 && old_pgroup_idx != parent_pgroup) {
                pgroup_entry_t *pg = PGROUP_ENTRY(old_pgroup_idx);
                pg->leader_count--;
            }

            /* Increment new leader count if different from parent's pgroup */
            if (new_pgroup_idx != parent_pgroup) {
                pgroup = PGROUP_ENTRY(new_pgroup_idx);
                pgroup->leader_count++;
            }
        }
    }

    /* TODO: Handle children's leader counts (iterating offset 0x20 list) */

    /* Set the new pgroup index */
    entry->pgroup_table_idx = new_pgroup_idx;
}

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

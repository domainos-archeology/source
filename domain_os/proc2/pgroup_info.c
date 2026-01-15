/*
 * PROC2_$PGROUP_INFO - Get process group information
 *
 * Returns the session ID and leader status for a process group.
 *
 * Parameters:
 *   pgroup_id  - Pointer to UPGID (process group ID)
 *   session_id - Pointer to receive session ID
 *   is_leader  - Pointer to receive leader flag (0xFF if has leader, 0 otherwise)
 *   status_ret - Pointer to receive status
 *
 * Special case: If pgroup_id is 0, returns session_id=0, is_leader=0xFF, status=ok
 *
 * Original address: 0x00e41dbc
 */

#include "proc2/proc2_internal.h"

/*
 * Internal helper: search pgroup table for entry with matching UPGID.
 * Returns index (1-69) if found, 0 if not found.
 * Original address: 0x00e42224
 */
static int16_t PGROUP_FIND_BY_UPGID(uint16_t upgid)
{
    int16_t i;

    /* Search all 69 slots (indices 1-69) */
    for (i = 1; i < PGROUP_TABLE_SIZE; i++) {
        pgroup_entry_t *entry = PGROUP_ENTRY(i);

        /* Skip free slots */
        if (entry->ref_count == 0) {
            continue;
        }

        /* Check if UPGID matches */
        if (entry->upgid == upgid) {
            return i;
        }
    }

    return 0;  /* Not found */
}

void PROC2_$PGROUP_INFO(uint16_t *pgroup_id, uint16_t *session_id_ret,
                        uint8_t *is_leader_ret, status_$t *status_ret)
{
    uint16_t upgid;
    int16_t pgroup_idx;
    int16_t proc_idx;
    proc2_info_t *entry;
    pgroup_entry_t *pgroup;
    status_$t status;
    uint16_t session_id;
    uint8_t is_leader;

    upgid = *pgroup_id;

    /* Special case: pgroup 0 means no group */
    if (upgid == 0) {
        *session_id_ret = 0;
        *is_leader_ret = 0xFF;  /* True - no leader means "is leader" vacuously */
        *status_ret = status_$ok;
        return;
    }

    ML_$LOCK(PROC2_LOCK_ID);

    /* First try to find in pgroup table directly */
    pgroup_idx = PGROUP_FIND_BY_UPGID(upgid);

    if (pgroup_idx == 0) {
        /*
         * Not found in pgroup table - search process list for a process
         * with this UPID to find its pgroup_table_idx
         */
        proc_idx = P2_INFO_ALLOC_PTR;
        while (proc_idx != 0) {
            entry = P2_INFO_ENTRY(proc_idx);

            if (upgid == entry->upid) {
                /* Found the process - get its pgroup index */
                pgroup_idx = entry->pgroup_table_idx;
                break;
            }

            proc_idx = entry->next_index;
        }

        if (pgroup_idx == 0) {
            /* Still not found */
            status = status_$proc2_uid_not_found;
            goto done;
        }
    }

    /* Get the pgroup entry */
    pgroup = PGROUP_ENTRY(pgroup_idx);

    /* Return session ID from pgroup table */
    session_id = pgroup->session_id;

    /* Leader flag: 0xFF if leader_count is 0, else 0 */
    is_leader = (pgroup->leader_count == 0) ? 0xFF : 0;

    status = status_$ok;

done:
    ML_$UNLOCK(PROC2_LOCK_ID);

    if (status == status_$ok) {
        *session_id_ret = session_id;
        *is_leader_ret = is_leader;
    }
    *status_ret = status;
}

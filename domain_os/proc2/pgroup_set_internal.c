/*
 * PGROUP_SET_INTERNAL - Set process's process group
 *
 * Internal helper to set a process's process group. Handles:
 * - Creating new pgroup table entries if needed
 * - Reference count management
 * - Leader count tracking (for orphan detection)
 *
 * Parameters:
 *   entry      - Pointer to the process's proc2_info_t
 *   new_upgid  - The new Unix process group ID (0 to leave current group)
 *   status_ret - Pointer to receive status
 *
 * Status codes:
 *   0x00000000: Success
 *   0x00190017: Process group is in a different session
 *
 * Original address: 0x00e41e86
 */

#include "proc2/proc2_internal.h"
#include "misc/misc.h"  /* For CRASH_SYSTEM */

/*
 * Memory access helpers for child iteration and field access.
 */
#if defined(ARCH_M68K)
    #define P2_PGROUP_IDX_FIELD(idx)    (*(int16_t*)(0xEA5448 + (idx) * 0xE4))   /* offset 0x10 */
    #define P2_SESSION_ID_FIELD(idx)    (*(int16_t*)(0xEA5494 + (idx) * 0xE4))   /* offset 0x5C */
    #define P2_CHILD_SIBLING_IDX(idx)   (*(int16_t*)(0xEA545A + (idx) * 0xE4))   /* offset 0x22 */
#else
    #define P2_PGROUP_IDX_FIELD(idx)    (P2_INFO_ENTRY(idx)->pgroup_table_idx)
    #define P2_SESSION_ID_FIELD(idx)    (P2_INFO_ENTRY(idx)->session_id)
    #define P2_CHILD_SIBLING_IDX(idx)   (P2_INFO_ENTRY(idx)->next_child_sibling)
#endif

void PGROUP_SET_INTERNAL(proc2_info_t *entry, uint16_t new_upgid, status_$t *status_ret)
{
    int16_t pgroup_idx;
    int16_t old_pgroup_idx;
    int16_t parent_pgroup_idx;
    int16_t child_idx;
    int16_t child_pgroup_idx;
    pgroup_entry_t *pgroup;
    pgroup_entry_t *old_pgroup;
    int16_t i;

    *status_ret = 0;

    /* If new_upgid is 0, clear the process group */
    if (new_upgid == 0) {
        PGROUP_CLEANUP_INTERNAL(entry, 2);
        entry->pgroup_table_idx = 0;
        return;
    }

    /* Search for existing pgroup with this UPGID */
    pgroup_idx = PGROUP_FIND_BY_UPGID(new_upgid);

    if (pgroup_idx == 0) {
        /*
         * No existing pgroup found - allocate a new slot.
         * Search for a free slot (ref_count == 0).
         */
        for (i = 1; i < PGROUP_TABLE_SIZE; i++) {
            if (PGROUP_ENTRY(i)->ref_count == 0) {
                break;
            }
        }

        /* Check if table is full */
        if (i >= PGROUP_TABLE_SIZE) {
            /* Table full - crash the system */
            static status_$t status_pgroup_table_full = status_$proc2_table_full;
            CRASH_SYSTEM(&status_pgroup_table_full);
            entry->pgroup_table_idx = 0;
            return;
        }

        pgroup_idx = i;

        /* Initialize the new pgroup entry */
        pgroup = PGROUP_ENTRY(pgroup_idx);
        pgroup->ref_count = 1;
        pgroup->leader_count = 0;
        pgroup->upgid = new_upgid;
        pgroup->session_id = entry->session_id;
    } else {
        /* Pgroup exists - verify session matches */
        pgroup = PGROUP_ENTRY(pgroup_idx);

        if ((int16_t)entry->session_id != (int16_t)pgroup->session_id) {
            *status_ret = status_$proc2_pgroup_in_different_session;
            return;
        }

        /* Increment reference count */
        pgroup->ref_count++;
    }

    /* Decrement ref_count on old pgroup if we had one */
    old_pgroup_idx = entry->pgroup_table_idx;
    if (old_pgroup_idx != 0) {
        old_pgroup = PGROUP_ENTRY(old_pgroup_idx);
        old_pgroup->ref_count--;
    }

    /*
     * Update leader counts based on parent relationship.
     * If our parent is in the same session but different pgroup,
     * we affect the leader count.
     */
    parent_pgroup_idx = entry->parent_pgroup_idx;

    if (parent_pgroup_idx != 0) {
        int16_t parent_entry_pgroup = P2_PGROUP_IDX_FIELD(parent_pgroup_idx);

        /* Check if parent is in the same session */
        if (P2_SESSION_ID_FIELD(parent_pgroup_idx) == entry->session_id) {
            /*
             * If old pgroup differs from parent's pgroup,
             * decrement leader count on old pgroup.
             */
            if (old_pgroup_idx != 0 && old_pgroup_idx != parent_entry_pgroup) {
                old_pgroup = PGROUP_ENTRY(old_pgroup_idx);
                old_pgroup->leader_count--;
            }

            /*
             * If new pgroup differs from parent's pgroup,
             * increment leader count on new pgroup.
             */
            if (pgroup_idx != parent_entry_pgroup) {
                pgroup->leader_count++;
            }
        }
    }

    /*
     * Update leader counts for all children.
     * For each child in the same session:
     * - If child's pgroup matched our old pgroup, increment their pgroup's leader count
     * - If child's pgroup matches our new pgroup, decrement their pgroup's leader count
     */
    child_idx = entry->first_child_idx;

    while (child_idx != 0) {
        /* Only consider children in the same session */
        if (P2_SESSION_ID_FIELD(child_idx) == entry->session_id) {
            child_pgroup_idx = P2_PGROUP_IDX_FIELD(child_idx);

            /*
             * If child was in a different pgroup than our old pgroup,
             * and our old pgroup matched theirs... wait, that's contradictory.
             * Let me re-read the original logic.
             *
             * Original check: if (child_pgroup != 0 && child_pgroup == old_pgroup_idx)
             *   then increment child's pgroup leader count
             *
             * This happens because: if our old pgroup == child's pgroup,
             * we were previously not a "leader" (same group as child).
             * Now that we're leaving that group, we become a "leader" relative
             * to that child's group, so increment their leader count.
             */
            if (child_pgroup_idx != 0 && child_pgroup_idx == old_pgroup_idx) {
                pgroup_entry_t *child_pgroup = PGROUP_ENTRY(child_pgroup_idx);
                child_pgroup->leader_count++;
            }

            /*
             * If child's pgroup matches our new pgroup,
             * we're now in the same group as the child.
             * Decrement their pgroup's leader count.
             */
            if (pgroup_idx == child_pgroup_idx) {
                pgroup_entry_t *child_pgroup = PGROUP_ENTRY(child_pgroup_idx);
                child_pgroup->leader_count--;
            }
        }

        /* Move to next sibling */
        child_idx = P2_CHILD_SIBLING_IDX(child_idx);
    }

    /* Finally, set the new pgroup index */
    entry->pgroup_table_idx = pgroup_idx;
}

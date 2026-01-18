/*
 * PGROUP_CLEANUP_INTERNAL - Clean up process group references
 *
 * Internal helper called when a process exits or changes process groups.
 * Handles the cleanup of pgroup references and leader count tracking.
 *
 * The mode parameter controls the cleanup behavior:
 *   mode = 0: Only update leader counts for children, don't decrement ref_count
 *   mode = 1: Only decrement ref_count and clear pgroup_table_idx, no leader cleanup
 *   mode = 2: Full cleanup (both leader counts and ref_count)
 *
 * Parameters:
 *   entry - Pointer to the process's proc2_info_t
 *   mode  - Cleanup mode (0, 1, or 2)
 *
 * Original address: 0x00e420b8
 */

#include "proc2/proc2_internal.h"

/*
 * Memory access helpers for child iteration.
 * Fields in proc2_info_t accessed via index-based addressing.
 */
#if defined(M68K)
    #define P2_PGROUP_IDX_FIELD(idx)    (*(int16_t*)(0xEA5448 + (idx) * 0xE4))   /* offset 0x10 */
    #define P2_SESSION_ID_FIELD(idx)    (*(int16_t*)(0xEA5494 + (idx) * 0xE4))   /* offset 0x5C */
    #define P2_CHILD_SIBLING_IDX(idx)   (*(int16_t*)(0xEA545A + (idx) * 0xE4))   /* offset 0x22 */
#else
    #define P2_PGROUP_IDX_FIELD(idx)    (P2_INFO_ENTRY(idx)->pgroup_table_idx)
    #define P2_SESSION_ID_FIELD(idx)    (P2_INFO_ENTRY(idx)->session_id)
    #define P2_CHILD_SIBLING_IDX(idx)   (P2_INFO_ENTRY(idx)->next_child_sibling)
#endif

void PGROUP_CLEANUP_INTERNAL(proc2_info_t *entry, int16_t mode)
{
    int16_t pgroup_idx;
    int16_t parent_pgroup_idx;
    int16_t child_idx;
    int16_t child_pgroup_idx;
    pgroup_entry_t *pgroup;

    /* If process has no pgroup, nothing to clean up */
    pgroup_idx = entry->pgroup_table_idx;
    if (pgroup_idx == 0) {
        return;
    }

    /* Get pgroup entry for ref_count decrement later */
    pgroup = PGROUP_ENTRY(pgroup_idx);

    /* Get parent's pgroup index for leader count calculations */
    parent_pgroup_idx = entry->parent_pgroup_idx;

    /* Mode 1 skips the leader count cleanup */
    if (mode == 1) {
        goto do_refcount;
    }

    /*
     * Check the parent process (at parent_pgroup_idx).
     * If the parent is in the same session but a different pgroup,
     * we need to decrement the leader count for our pgroup.
     */
    if (parent_pgroup_idx != 0) {
        /* Check if parent's pgroup is different from ours */
        if (P2_PGROUP_IDX_FIELD(parent_pgroup_idx) != pgroup_idx) {
            /* Check if parent is in the same session */
            if (P2_SESSION_ID_FIELD(parent_pgroup_idx) == entry->session_id) {
                PGROUP_DECR_LEADER_COUNT(pgroup_idx);
            }
        }
    }

    /*
     * Iterate through children (linked via first_child_idx -> next_child_sibling).
     * For each child in the same session but different pgroup, decrement the
     * leader count of the child's pgroup.
     */
    child_idx = entry->first_child_idx;

    while (child_idx != 0) {
        child_pgroup_idx = P2_PGROUP_IDX_FIELD(child_idx);

        /* Skip if child's pgroup is the same as ours */
        if (child_pgroup_idx != pgroup_idx) {
            /* Check if child is in the same session */
            if (P2_SESSION_ID_FIELD(child_idx) == entry->session_id) {
                PGROUP_DECR_LEADER_COUNT(child_pgroup_idx);
            }
        }

        /* Move to next sibling */
        child_idx = P2_CHILD_SIBLING_IDX(child_idx);
    }

do_refcount:
    /* Mode 0 skips the ref_count decrement */
    if (mode == 0) {
        return;
    }

    /* Decrement reference count and clear pgroup_table_idx */
    pgroup->ref_count--;
    entry->pgroup_table_idx = 0;
}

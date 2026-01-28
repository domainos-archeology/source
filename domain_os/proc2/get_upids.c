/*
 * PROC2_$GET_UPIDS - Get Unix PIDs for process
 *
 * Returns the Unix-style PIDs for a process, its process group leader,
 * and its parent process.
 *
 * Parameters:
 *   proc_uid - UID of process to query
 *   upid_ret - Pointer to receive process UPID
 *   upgid_ret - Pointer to receive process group UPID
 *   uppid_ret - Pointer to receive parent process UPID
 *   status_ret - Status return
 *
 * Notes:
 *   - If pgroup_idx is 0, returns 1 for upgid (init process)
 *   - If parent_idx is 0, returns 0 for uppid (no parent)
 *   - Parent UPID is looked up via a separate table (8-byte entries)
 *
 * Original address: 0x00e738a8
 */

#include "proc2/proc2_internal.h"

/*
 * Raw memory access macros for parent-child fields
 */
#if defined(ARCH_M68K)
    #define P2_CHILD_BASE(idx)      ((int16_t*)(0xEA551C + ((idx) * 0xE4)))
    #define P2_PARENT_IDX(idx)      (*(P2_CHILD_BASE(idx) - 0x63))
    #define P2_PARENT_UPID(idx)     (*(int16_t*)(0xEA944E + (idx) * 8))
#else
    static int16_t p2_dummy_field;
    #define P2_PARENT_IDX(idx)      (p2_dummy_field)
    #define P2_PARENT_UPID(idx)     (p2_dummy_field)
#endif

void PROC2_$GET_UPIDS(uid_t *proc_uid, uint16_t *upid_ret, uint16_t *upgid_ret,
                      uint16_t *uppid_ret, status_$t *status_ret)
{
    int16_t index;
    proc2_info_t *entry;
    status_$t status;
    uint16_t upid = 0;
    uint16_t upgid = 0;
    uint16_t uppid = 0;
    uid_t uid;

    /* Copy UID to local storage */
    uid.high = proc_uid->high;
    uid.low = proc_uid->low;

    ML_$LOCK(PROC2_LOCK_ID);

    index = PROC2_$FIND_INDEX(&uid, &status);

    if (status == status_$ok) {
        entry = P2_INFO_ENTRY(index);

        /* Get process's own UPID */
        upid = entry->upid;

        /* Get process group's UPID */
        if (entry->pgroup_table_idx == 0) {
            /* No process group - return 1 (init) */
            upgid = 1;
        } else {
            /* Look up pgroup leader's UPID */
            proc2_info_t *pgroup_entry = P2_INFO_ENTRY(entry->pgroup_table_idx);
            upgid = pgroup_entry->upid;
        }

        /* Get parent's UPID */
        if (P2_PARENT_IDX(index) == 0) {
            /* No parent - return 0 */
            uppid = 0;
        } else {
            /* Look up parent's UPID via separate table */
            uppid = P2_PARENT_UPID(P2_PARENT_IDX(index));
        }
    }

    ML_$UNLOCK(PROC2_LOCK_ID);

    *upid_ret = upid;
    *upgid_ret = upgid;
    *uppid_ret = uppid;
    *status_ret = status;
}

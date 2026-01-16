/*
 * PROC2_$GET_MY_UPIDS - Get Unix PIDs for current process
 *
 * Returns the current process's UPID, UPGID (process group UPID),
 * and UPPID (parent UPID).
 *
 * Parameters:
 *   upid - Pointer to receive current process UPID
 *   upgid - Pointer to receive process group UPID (1 if no pgroup)
 *   uppid - Pointer to receive parent UPID (0 if no parent)
 *
 * Original address: 0x00e73968
 */

#include "proc2/proc2_internal.h"

/*
 * Raw memory access macros for parent-child fields
 * P2_PARENT_IDX: parent index at offset 0x1E from entry base
 * P2_PARENT_UPID: parent UPID in separate table (8-byte entries)
 */
#if defined(M68K)
    #define P2_CHILD_BASE(idx)      ((int16_t*)(0xEA551C + ((idx) * 0xE4)))
    #define P2_PARENT_IDX(idx)      (*(P2_CHILD_BASE(idx) - 0x63))
    #define P2_PARENT_UPID(idx)     (*(int16_t*)(0xEA944E + (idx) * 8))
#else
    static int16_t p2_dummy_field;
    #define P2_PARENT_IDX(idx)      (p2_dummy_field)
    #define P2_PARENT_UPID(idx)     (p2_dummy_field)
#endif

void PROC2_$GET_MY_UPIDS(uint16_t *upid, uint16_t *upgid, uint16_t *uppid)
{
    int16_t my_index;
    proc2_info_t *entry;
    proc2_info_t *pgroup_entry;

    /* Get my proc2 index from PID mapping table */
    my_index = P2_PID_TO_INDEX(PROC1_$CURRENT);

    entry = P2_INFO_ENTRY(my_index);

    /* Return my UPID */
    *upid = entry->upid;

    /* Return process group UPID */
    if (entry->pgroup_table_idx == 0) {
        *upgid = 1;  /* Default UPGID when no pgroup */
    } else {
        pgroup_entry = P2_INFO_ENTRY(entry->pgroup_table_idx);
        *upgid = pgroup_entry->upid;
    }

    /* Return parent UPID */
    if (P2_PARENT_IDX(my_index) == 0) {
        *uppid = 0;  /* No parent */
    } else {
        /* Lookup in parent UPID table (8-byte entries) */
        *uppid = P2_PARENT_UPID(P2_PARENT_IDX(my_index));
    }
}

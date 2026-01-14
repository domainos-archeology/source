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

#include "proc2.h"

/* External reference to current PROC1 process */
extern uint16_t PROC1_CURRENT;

void PROC2_$GET_MY_UPIDS(uint16_t *upid, uint16_t *upgid, uint16_t *uppid)
{
    int16_t my_index;
    proc2_info_t *entry;
    proc2_info_t *pgroup_entry;

    /* Get my proc2 index from PID mapping table */
    my_index = P2_PID_TO_INDEX(PROC1_CURRENT);

    entry = P2_INFO_ENTRY(my_index);

    /* Return my UPID */
    *upid = entry->upid;

    /* Return process group UPID */
    if (entry->pgroup_idx == 0) {
        *upgid = 1;  /* Default UPGID when no pgroup */
    } else {
        pgroup_entry = P2_INFO_ENTRY(entry->pgroup_idx);
        *upgid = pgroup_entry->upid;
    }

    /* Return parent UPID */
    if (entry->parent_idx == 0) {
        *uppid = 0;  /* No parent */
    } else {
        /* Lookup in parent UPID table (8-byte entries) */
        *uppid = P2_PARENT_UPID(entry->parent_idx);
    }
}

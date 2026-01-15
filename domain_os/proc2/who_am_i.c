/*
 * PROC2_$WHO_AM_I - Get current process UID
 *
 * Looks up the current process in the PROC2 table and returns its UID.
 * Uses the PID-to-index mapping table to find the PROC2 entry.
 *
 * Parameters:
 *   proc_uid - Pointer to receive the process UID
 *
 * Original address: 0x00e73862
 */

#include "proc2/proc2_internal.h"

void PROC2_$WHO_AM_I(uid_t *proc_uid)
{
    int16_t index;
    proc2_info_t *entry;

    /* Get PROC2 index from PROC1 PID mapping table */
    index = P2_PID_TO_INDEX(PROC1_$CURRENT);

    /* Get entry from process info table */
    entry = P2_INFO_ENTRY(index);

    /* Copy UID to output */
    proc_uid->high = entry->uid.high;
    proc_uid->low = entry->uid.low;
}

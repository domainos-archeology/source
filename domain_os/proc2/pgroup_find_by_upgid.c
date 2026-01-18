/*
 * PGROUP_FIND_BY_UPGID - Find process group by Unix process group ID
 *
 * Internal helper that searches the pgroup table for an entry with
 * the matching UPGID. Returns the index (1-69) if found, 0 if not found.
 *
 * Parameters:
 *   upgid - The Unix process group ID to search for
 *
 * Returns:
 *   Index into pgroup table (1-69) if found, 0 if not found
 *
 * Original address: 0x00e42224
 */

#include "proc2/proc2_internal.h"

int16_t PGROUP_FIND_BY_UPGID(uint16_t upgid)
{
    int16_t i;

    /* Search all 69 slots (indices 1-69) */
    for (i = 1; i < PGROUP_TABLE_SIZE; i++) {
        pgroup_entry_t *entry = PGROUP_ENTRY(i);

        /* Skip free slots (ref_count == 0) */
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

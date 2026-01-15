/*
 * PROC2_$FIND_INDEX - Find process table index by UID
 *
 * Searches the process info table for a process with the given UID.
 * The table is a linked list traversed via the next_index field.
 *
 * Parameters:
 *   proc_uid - UID to search for
 *   status_ret - Status return
 *
 * Returns:
 *   Index in process table (1-based), or 0 if not found
 *
 * Status codes:
 *   status_$ok - Process found
 *   status_$proc2_uid_not_found - UID not in table
 *   status_$proc2_zombie - Process is zombie
 *
 * Original address: 0x00e4068e
 */

#include "proc2/proc2_internal.h"

int16_t PROC2_$FIND_INDEX(uid_t *proc_uid, status_$t *status_ret)
{
    int16_t index;
    proc2_info_t *entry;

    /* Start at the allocation pointer (head of active list) */
    index = P2_INFO_ALLOC_PTR;

    while (index != 0) {
        entry = P2_INFO_ENTRY(index);

        /* Compare UIDs */
        if (proc_uid->high == entry->uid.high &&
            proc_uid->low == entry->uid.low) {
            /* Found it - check if zombie */
            if ((entry->flags & PROC2_FLAG_ZOMBIE) != 0) {
                *status_ret = status_$proc2_zombie;
                return index;
            }
            *status_ret = status_$ok;
            return index;
        }

        /* Follow link to next entry */
        index = entry->next_index;
    }

    /* Not found */
    *status_ret = status_$proc2_uid_not_found;
    return 0;
}

/*
 * PROC2_$UPID_TO_UID - Convert Unix PID to UID
 *
 * Searches the process allocation list for a matching UPID and
 * returns the corresponding UID.
 *
 * Parameters:
 *   upid - Pointer to UPID to search for
 *   uid_ret - Pointer to receive process UID
 *   status_ret - Status return
 *
 * Status codes:
 *   status_$ok - Success
 *   proc2_$uid_not_found - No process with matching UPID
 *   proc2_$zombie - Process found but is a zombie
 *
 * Original address: 0x00e40ece
 */

#include "proc2/proc2_internal.h"

void PROC2_$UPID_TO_UID(int16_t *upid, uid_t *uid_ret, status_$t *status_ret)
{
    int16_t search_upid;
    int16_t index;
    proc2_info_t *entry;
    status_$t status;
    uid_t found_uid;

    search_upid = *upid;
    status = status_$ok;
    found_uid.high = 0;
    found_uid.low = 0;

    ML_$LOCK(PROC2_LOCK_ID);

    /* Iterate through allocation list */
    index = P2_INFO_ALLOC_PTR;
    while (index != 0) {
        entry = P2_INFO_ENTRY(index);

        /* Check for matching UPID */
        if (entry->upid == search_upid) {
            /* Found it - copy UID */
            found_uid.high = entry->uid.high;
            found_uid.low = entry->uid.low;

            /* Check if zombie */
            if ((entry->flags & PROC2_FLAG_ZOMBIE) != 0) {
                status = status_$proc2_zombie;
            }
            goto done;
        }

        /* Next entry in allocation list */
        index = entry->next_index;
    }

    /* Not found */
    status = status_$proc2_uid_not_found;

done:
    ML_$UNLOCK(PROC2_LOCK_ID);

    uid_ret->high = found_uid.high;
    uid_ret->low = found_uid.low;
    *status_ret = status;
}

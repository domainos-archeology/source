/*
 * PROC2_$UID_TO_UPID - Convert UID to Unix PID
 *
 * Searches the process allocation list for a matching UID and
 * returns the corresponding UPID.
 *
 * Parameters:
 *   proc_uid - Pointer to UID to search for
 *   upid_ret - Pointer to receive UPID
 *   status_ret - Status return
 *
 * Status codes:
 *   status_$ok - Success
 *   proc2_$uid_not_found - No process with matching UID
 *   proc2_$zombie - Process found but is a zombie
 *
 * Original address: 0x00e40f6c
 */

#include "proc2.h"

void PROC2_$UID_TO_UPID(uid_t *proc_uid, uint16_t *upid_ret, status_$t *status_ret)
{
    uint32_t search_high;
    uint32_t search_low;
    int16_t index;
    proc2_info_t *entry;
    status_$t status;
    uint16_t found_upid;

    search_high = proc_uid->high;
    search_low = proc_uid->low;
    status = status_$ok;
    found_upid = 0;

    ML_$LOCK(PROC2_LOCK_ID);

    /* Iterate through allocation list */
    index = P2_INFO_ALLOC_PTR;
    while (index != 0) {
        entry = P2_INFO_ENTRY(index);

        /* Check for matching UID */
        if (entry->uid.high == search_high && entry->uid.low == search_low) {
            /* Found it - get UPID */
            found_upid = entry->upid;

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

    *upid_ret = found_upid;
    *status_ret = status;
}

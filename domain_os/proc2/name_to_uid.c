/*
 * PROC2_$NAME_TO_UID - Look up process by name
 *
 * Searches for a process with the given name and returns its UID.
 *
 * Parameters:
 *   name - Process name to search for
 *   name_len - Pointer to name length (0-32)
 *   uid_ret - Pointer to receive process UID
 *   status_ret - Status return
 *
 * Original address: 0x00e3ead0
 */

#include "proc2.h"

void PROC2_$NAME_TO_UID(char *name, int16_t *name_len, uid_t *uid_ret, status_$t *status_ret)
{
    int16_t len;
    int16_t index;
    proc2_info_t *entry;
    int16_t i;
    int match;

    len = *name_len;

    /* Validate name length */
    if (len < 0 || len > 32) {
        *status_ret = status_$proc2_invalid_process_name;
        return;
    }

    *status_ret = status_$ok;

    ML_$LOCK(PROC2_LOCK_ID);

    /* Iterate through allocated process entries */
    index = P2_INFO_ALLOC_PTR;

    while (index != 0) {
        entry = P2_INFO_ENTRY(index);

        /* Check if name length matches */
        if ((uint8_t)entry->name_len == len) {
            /* Compare names */
            match = 1;
            for (i = 0; i < len; i++) {
                if (entry->name[i] != name[i]) {
                    match = 0;
                    break;
                }
            }

            if (match) {
                /* Found matching process */
                ML_$UNLOCK(PROC2_LOCK_ID);
                uid_ret->high = entry->uid.high;
                uid_ret->low = entry->uid.low;
                return;
            }
        }

        /* Move to next entry in allocation list */
        index = entry->next_index;
    }

    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status_$proc2_uid_not_found;
}

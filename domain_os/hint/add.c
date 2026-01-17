/*
 * HINT_$ADD - Add hint with address pointer
 *
 * Adds hint information for a file UID. The address data is copied from
 * the pointed-to location.
 *
 * Original address: 0x00E49BB8
 */

#include "hint/hint_internal.h"

void HINT_$ADD(uid_t *uid_ptr, uint32_t *addr_ptr)
{
    hint_addr_t local_addr;

    /* Early exit if hint file not mapped */
    if (HINT_$HINTFILE_PTR == NULL) {
        return;
    }

    /* Copy the address data locally */
    local_addr.node_id = *addr_ptr;
    local_addr.flags = 0;

    /* Acquire exclusion lock */
    ML_$EXCLUSION_START(&HINT_$EXCLUSION_LOCK);

    /* Add the hint */
    HINT_$add_internal(uid_ptr, &local_addr);

    /* Release exclusion lock */
    ML_$EXCLUSION_STOP(&HINT_$EXCLUSION_LOCK);
}

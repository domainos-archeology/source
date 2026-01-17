/*
 * HINT_$ADDI - Add hint with inline address
 *
 * Adds hint information for a file UID. The addresses parameter is passed
 * directly (not as a pointer to be dereferenced).
 *
 * Original address: 0x00E49B74
 */

#include "hint/hint_internal.h"

void HINT_$ADDI(uid_t *uid_ptr, uint32_t *addresses)
{
    /* Early exit if hint file not mapped */
    if (HINT_$HINTFILE_PTR == NULL) {
        return;
    }

    /* Acquire exclusion lock */
    ML_$EXCLUSION_START(&HINT_$EXCLUSION_LOCK);

    /* Add the hint */
    HINT_$add_internal(uid_ptr, (hint_addr_t *)addresses);

    /* Release exclusion lock */
    ML_$EXCLUSION_STOP(&HINT_$EXCLUSION_LOCK);
}

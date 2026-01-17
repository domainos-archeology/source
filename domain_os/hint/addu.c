/*
 * HINT_$ADDU - Add hint with UID lookup
 *
 * Looks up existing hints for source_uid, then adds those hints to
 * target_uid. Used when a file reference is followed to add hints
 * from the source to the target.
 *
 * Original address: 0x00E49C08
 */

#include "hint/hint_internal.h"

void HINT_$ADDU(uid_t *target_uid, uid_t *source_uid)
{
    hint_addr_t addresses[5];  /* Room for up to 5 hint addresses (40 bytes) */
    uint32_t target_key;
    uint32_t source_key;

    /* Early exit if hint file not mapped */
    if (HINT_$HINTFILE_PTR == NULL) {
        return;
    }

    /* Check if target and source have the same key - if so, skip */
    target_key = target_uid->low & HINT_UID_MASK;
    source_key = source_uid->low & HINT_UID_MASK;

    if (target_key == source_key) {
        return;  /* No point adding hints from same location */
    }

    /* Get hints for the source UID */
    HINT_$GET_HINTS(source_uid, (uint32_t *)addresses);

    /* Acquire exclusion lock */
    ML_$EXCLUSION_START(&HINT_$EXCLUSION_LOCK);

    /* Add the hints to target UID */
    HINT_$add_internal(target_uid, addresses);

    /* Release exclusion lock */
    ML_$EXCLUSION_STOP(&HINT_$EXCLUSION_LOCK);
}

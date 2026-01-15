/*
 * AST_$ASSOC - Associate a physical page with an object page
 *
 * High-level interface to associate a physical page with a virtual page
 * in an object. Activates and wires the segment, then calls AST_$PMAP_ASSOC
 * to do the actual mapping. Retries with AST_$TOUCH if needed.
 *
 * Parameters:
 *   uid - Pointer to object UID
 *   seg - Segment number
 *   mode - Access mode/concurrency token
 *   page - Page number within segment (0-31)
 *   flags - Operation flags
 *   ppn - Physical page number to associate
 *   status - Status return
 *
 * Original address: 0x00e0444e
 */

#include "ast/ast_internal.h"

void AST_$ASSOC(uid_t *uid, uint16_t seg, uint32_t mode, uint16_t page,
                uint16_t flags, uint32_t ppn, status_$t *status)
{
    aste_t *aste;
    aote_t *aote;
    uint32_t ppn_array[32];

    /* Activate and wire the segment */
    aste = AST_$ACTIVATE_AND_WIRE(uid, seg, status);

    if (aste == NULL) {
        return;
    }

    ML_$LOCK(PMAP_LOCK_ID);

    aote = aste->aote;

    /* Check for remote object access restriction */
    if (*(int8_t *)((char *)aote + 0xB9) < 0) {
        /* Remote object - check process type */
        if (*(int16_t *)((char *)PROC1_$TYPE + (int16_t)(PROC1_$CURRENT * 2)) == 8) {
            *status = status_$file_object_not_found;
            goto done;
        }
    }

    /* Mark AOTE as busy */
    aote->flags |= AOTE_FLAG_BUSY;

    /* Check concurrency */
    uint32_t concurrency = *(uint32_t *)((char *)aote + 0x50);
    if (concurrency == mode || concurrency == 1) {
        /* Try to associate the page */
        do {
            AST_$PMAP_ASSOC(aste, page, ppn, 0, 0, status);

            if (*status != status_$pmap_bad_assoc) {
                break;
            }

            /* Page not ready - touch it first */
            AST_$TOUCH(aste, mode, page, 1, ppn_array, status, flags | 0x42);

        } while (*status == status_$ok || *status == status_$pmap_page_null);
    } else {
        *status = status_$ast_write_concurrency_violation;
    }

done:
    ML_$UNLOCK(PMAP_LOCK_ID);

    /* Decrement wire count */
    aste->wire_count--;
}

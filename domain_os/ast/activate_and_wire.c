/*
 * AST_$ACTIVATE_AND_WIRE - Activate and wire an ASTE
 *
 * Finds or creates an ASTE for the given UID and segment,
 * then increments its wire count to prevent deactivation.
 *
 * Original address: 0x00e02fb8
 */

#include "ast/ast_internal.h"

aste_t* AST_$ACTIVATE_AND_WIRE(uid_t *uid, uint16_t seg, status_$t *status)
{
    aote_t *aote;
    aste_t *aste;

    ML_$LOCK(AST_LOCK_ID);
    *status = status_$ok;

    /* Look up AOTE by UID */
    aote = ast_$lookup_aote_by_uid(uid);

    if (aote == NULL) {
        /* AOTE not found - try to create/load it */
        aote = ast_$force_activate_segment(uid, 0, status, 0);
        if (aote == NULL) {
            ML_$UNLOCK(AST_LOCK_ID);
            return NULL;
        }
    }

    /* Find ASTE for this segment */
    aste = ast_$lookup_aste(aote, seg);

    if (aste == NULL) {
        /* ASTE not found - try to create it */
        aste = ast_$lookup_or_create_aste(aote, seg, status);
        if (aste == NULL) {
            ML_$UNLOCK(AST_LOCK_ID);
            return NULL;
        }
    }

    /* Increment wire count */
    aste->wire_count++;

    ML_$UNLOCK(AST_LOCK_ID);
    return aste;
}

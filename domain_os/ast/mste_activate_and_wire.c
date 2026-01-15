/*
 * AST_$MSTE_ACTIVATE_AND_WIRE - Activate and wire from MSTE
 *
 * Activates and wires an ASTE using information from an MSTE
 * (Memory Segment Table Entry) structure.
 *
 * Original address: 0x00e02f34
 */

#include "ast/ast_internal.h"

aste_t* AST_$MSTE_ACTIVATE_AND_WIRE(mste_t *mste, status_$t *status)
{
    aote_t *aote;
    aste_t *aste;

    *status = status_$ok;

    /* Look up AOTE by UID */
    aote = ast_$lookup_aote_by_uid(&mste->uid);

    if (aote == NULL) {
        /* AOTE not found - try to create/load it */
        aote = ast_$force_activate_segment(&mste->uid, mste->vol_uid, status, 0);
        if (aote == NULL) {
            return NULL;
        }
    }

    /* Find ASTE for this segment */
    aste = ast_$lookup_aste(aote, mste->segment);

    if (aste == NULL) {
        /* ASTE not found - try to create it */
        aste = ast_$lookup_or_create_aste(aote, mste->segment, status);
        if (aste == NULL) {
            return NULL;
        }
    }

    /* Increment wire count */
    aste->wire_count++;

    return aste;
}

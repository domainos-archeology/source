/*
 * AST_$MSTE_ACTIVATE_AND_WIRE - Activate and wire from MSTE
 *
 * Activates and wires an ASTE using information from an MSTE
 * (Memory Segment Table Entry) structure.
 *
 * Original address: 0x00e02f34
 */

#include "ast.h"

/* Internal function prototypes */
extern aote_t* FUN_00e0209e(uid_t *uid);
extern aote_t* FUN_00e020fa(uid_t *uid, uint32_t vol_uid, status_$t *status, int8_t flag);
extern aste_t* FUN_00e0250c(aote_t *aote, uint16_t seg);
extern aste_t* FUN_00e0255c(aote_t *aote, uint16_t seg, status_$t *status);

aste_t* AST_$MSTE_ACTIVATE_AND_WIRE(mste_t *mste, status_$t *status)
{
    aote_t *aote;
    aste_t *aste;

    *status = status_$ok;

    /* Look up AOTE by UID */
    aote = FUN_00e0209e(&mste->uid);

    if (aote == NULL) {
        /* AOTE not found - try to create/load it */
        aote = FUN_00e020fa(&mste->uid, mste->vol_uid, status, 0);
        if (aote == NULL) {
            return NULL;
        }
    }

    /* Find ASTE for this segment */
    aste = FUN_00e0250c(aote, mste->segment);

    if (aste == NULL) {
        /* ASTE not found - try to create it */
        aste = FUN_00e0255c(aote, mste->segment, status);
        if (aste == NULL) {
            return NULL;
        }
    }

    /* Increment wire count */
    aste->wire_count++;

    return aste;
}

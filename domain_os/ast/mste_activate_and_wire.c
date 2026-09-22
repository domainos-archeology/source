/*
 * AST_$MSTE_ACTIVATE_AND_WIRE - Activate and wire the ASTE an MSTE names
 *
 * The AST-lock-held twin of AST_$ACTIVATE_AND_WIRE that takes its UID,
 * segment and location from an mste_t: finds (or activates, with the
 * MSTE's location word) the AOTE, finds (or creates) the ASTE for the
 * MSTE's segment, and bumps that ASTE's wire count.  Returns the ASTE or
 * NULL; the failing callee's status stands.
 *
 * Parameters (frame at 0x00E02F34, `link.w A6,-0xc`):
 *   mste   (0x8,A6)  (A2): uid +0x00, segment +0x08 (word), location +0x0C
 *   status (0xC,A6)  (D2), cleared first
 *
 * Original address: 0x00E02F34 (132 bytes), A5 = 0xE1DC80 (unused here).
 * One exit: `suba.l A0,A0` at 0x00E02FA6 joins the epilogue at 0x00E02FAE.
 */

#include "ast/ast_internal.h"

aste_t *AST_$MSTE_ACTIVATE_AND_WIRE(mste_t *mste, status_$t *status)
{
    aote_t *aote;               /* A3 */
    aste_t *aste;               /* A0 */

    /* 0x00E02F4A..0x00E02F4C */
    *status = status_$ok;

    /* 0x00E02F4E..0x00E02F5C */
    aote = ast_$lookup_aote_by_uid(&mste->uid);
    if (aote == NULL) {
        /* 0x00E02F5E..0x00E02F78: the MSTE's location word, force := 0 */
        aote = ast_$force_activate_segment(&mste->uid, mste->location,
                                           status, 0);
        if (aote == NULL) {
            return NULL;                            /* 0x00E02FA6 */
        }
    }

    /* 0x00E02F7A..0x00E02F8C */
    aste = ast_$lookup_aste(aote, (int16_t)mste->segment);
    if (aste == NULL) {
        /* 0x00E02F8E..0x00E02FA4 */
        aste = ast_$lookup_or_create_aste(aote, mste->segment, status);
        if (aste == NULL) {
            return NULL;                            /* 0x00E02FA6 */
        }
    }

    /* 0x00E02FAA: addq.b #0x1,(0x11,A0) */
    aste->wire_count++;
    return aste;
}

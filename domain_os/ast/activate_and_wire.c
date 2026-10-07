/*
 * AST_$ACTIVATE_AND_WIRE - Activate and wire an ASTE
 *
 * Finds (or activates) the AOTE for `uid`, finds (or creates) the ASTE for
 * `seg` under it, and bumps that ASTE's wire count so it cannot be
 * deactivated.  Returns the ASTE, or NULL when either step fails; the
 * failure status is whatever the failing callee wrote through `status`.
 *
 * Original address: 0x00E02FB8 (164 bytes), A5 = 0xE1DC80 (AST_ module
 * block; nothing in it is touched here).
 *
 * Frame: (0x8,A6) uid (A4), (0xC,A6) seg word, (0xE,A6) status (A3),
 * (-0x4,A6) a longword cleared to 0 and passed as the `location` argument
 * of ast_$force_activate_segment.
 *
 * The image has ONE exit: every path falls into ML_$UNLOCK at 0x00E03044
 * with the result in A2 (0x00E0303A `suba.l A2,A2` for failure,
 * 0x00E0303E..0x00E03042 for success), so the C keeps a single unlock.
 */

#include "ast/ast_internal.h"

aste_t *AST_$ACTIVATE_AND_WIRE(uid_t *uid, uint16_t seg, status_$t *status)
{
    uint32_t location;      /* (-0x4,A6) */
    aote_t *aote;           /* A2 */
    aste_t *aste;           /* A0 */
    aste_t *result;         /* A2 */

    /* 0x00E02FD0..0x00E02FDC: ML_$LOCK(0x12); *status := status_$ok */
    ML_$LOCK(AST_LOCK_ID);
    *status = status_$ok;

    /* 0x00E02FDE..0x00E02FEC: AOTE lookup (result in A0) */
    aote = ast_$lookup_aote_by_uid(uid);
    if (aote == NULL) {
        /*
         * 0x00E02FEE..0x00E0300C: not resident - force-activate it with a
         * zero (unknown) location and force := 0 (the `clr.w -(SP)` at
         * 0x00E02FF4 is the byte parameter's word slot).
         */
        location = 0;
        aote = ast_$force_activate_segment(uid, location, status, 0);
        if (aote == NULL) {
            result = NULL;                      /* 0x00E0303A */
            goto unlock;
        }
    }

    /* 0x00E0300E..0x00E03020: existing ASTE for this segment? */
    aste = ast_$lookup_aste(aote, (int16_t)seg);
    if (aste == NULL) {
        /* 0x00E03022..0x00E03038: no - create one */
        aste = ast_$lookup_or_create_aste(aote, seg, status);
        if (aste == NULL) {
            result = NULL;                      /* 0x00E0303A */
            goto unlock;
        }
    }

    /* 0x00E0303E..0x00E03042: addq.b #1,(0x11,A0) - byte wire count */
    aste->wire_count++;
    result = aste;

unlock:
    /* 0x00E03044..0x00E03050: ML_$UNLOCK(0x12); return A2 in A0 (and D0) */
    ARCH_RESULT_A0(result);
    ML_$UNLOCK(AST_LOCK_ID);
    return result;
}

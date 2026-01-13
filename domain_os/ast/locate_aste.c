/*
 * AST_$LOCATE_ASTE - Locate an ASTE by UID and segment
 *
 * Attempts a fast lookup of an ASTE using a hint, falling back
 * to a full search if the hint is invalid.
 *
 * Original address: 0x00e07050
 */

#include "ast.h"

/* Internal function prototypes */
extern aote_t* FUN_00e0209e(uid_t *uid);
extern aste_t* FUN_00e0250c(aote_t *aote, uint16_t seg);

aste_t* AST_$LOCATE_ASTE(locate_request_t *request)
{
    uint16_t hint_index;
    aste_t *aste;
    aote_t *aote;

    /* Extract hint index (low 9 bits) */
    hint_index = request->hint & ASTE_INDEX_MASK;

    /* Try using the hint first */
    if (hint_index != 0 && hint_index <= AST_$SIZE_AST) {
        /* Calculate ASTE address from index */
        aste = (aste_t *)((char *)ASTE_BASE + (hint_index - 1) * sizeof(aste_t));

        /* Validate the hint */
        if ((int16_t)aste->flags >= 0 &&                    /* Not in transition */
            aste->segment == request->segment &&             /* Segment matches */
            (aste->flags & ASTE_FLAG_AREA) == 0 &&          /* Not an area mapping */
            aste->aote != NULL &&                            /* Has valid AOTE */
            (int8_t)aste->aote->flags >= 0) {               /* AOTE not in transition */

            /* Verify UID matches */
            if (*(uint32_t *)((char *)aste->aote + 0x10) == request->uid_high &&
                *(uint32_t *)((char *)aste->aote + 0x14) == request->uid_low) {
                return aste;
            }
        }
    }

    /* Hint invalid or didn't match - do full lookup */
    aote = FUN_00e0209e((uid_t *)request);

    if (aote == NULL) {
        return NULL;
    }

    /* Search for ASTE with matching segment */
    return FUN_00e0250c(aote, request->segment);
}

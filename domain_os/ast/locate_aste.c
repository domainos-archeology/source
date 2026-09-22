/*
 * AST_$LOCATE_ASTE - Find an ASTE from a {uid, segment, hint} request
 *
 * The request's hint (low 9 bits of +0x0A) names an ASTE index; if it is
 * in range and that ASTE is not in transition, carries the wanted
 * segment, is not an AREA entry, and belongs to an AOTE that is not in
 * transition and has the wanted UID, the ASTE is returned at once.
 * Otherwise the object is looked up by UID and its ASTE list searched.
 *
 * Original address: 0x00E07050 (156 bytes), A5 = 0xE1DC80:
 *   (0x470,A5) AST_$SIZE_AST.  Frame: (0x8,A6) request (A2).
 */

#include "ast/ast_internal.h"

aste_t *AST_$LOCATE_ASTE(locate_request_t *request)
{
    uint16_t hint_index;        /* D0w */
    aste_t *aste;               /* A0 */
    aote_t *aote;               /* A1 / A3 */

    /* 0x00E07062..0x00E07070: the hint, 1..AST_$SIZE_AST */
    hint_index = request->hint & ASTE_INDEX_MASK;
    if (hint_index != 0 && hint_index <= AST_$SIZE_AST) {
        /* 0x00E07072..0x00E07084: 0xEC5400 + hint*0x14 - 0x14 */
        aste = &ASTE_BASE[hint_index - 1];

        /* 0x00E07088..0x00E070C0 */
        if ((int16_t)aste->flags >= 0 &&
            aste->segment == request->segment &&        /* (0xc,A0) */
            (aste->flags & ASTE_FLAG_AREA) == 0 &&      /* btst.l #0xc */
            aste->aote != NULL) {
            aote = aste->aote;
            if ((int8_t)aote->flags >= 0 &&             /* tst.b (0xbf,A1) */
                aote->uid.high == request->uid_high &&  /* two cmpm.l */
                aote->uid.low == request->uid_low) {
                return aste;                            /* 0x00E070E2 */
            }
        }
    }

    /* 0x00E070C2..0x00E070D4: the request starts with the UID */
    aote = ast_$lookup_aote_by_uid((uid_t *)request);
    if (aote == NULL) {
        return NULL;
    }

    /* 0x00E070D6..0x00E070DE: the result is whatever lookup_aste returns */
    return ast_$lookup_aste(aote, (int16_t)request->segment);
}

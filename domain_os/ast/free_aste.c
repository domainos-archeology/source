/*
 * AST_$FREE_ASTE - Return an ASTE to the free list
 *
 * Decrements the AREA / remote / local ASTE counter the entry's flags
 * select, drops its AOTE link, pushes it on the free list, marks it
 * in-transition (the free-list marker, flags bit 15) and advances the AST
 * in-transition eventcount.
 *
 * Original address: 0x00E00FBC (88 bytes).  `link.w A6,0x0` / `pea (A5)` /
 * `lea (0xe1dc80).l,A5`: A5 = the AST_ block.
 *   (0x3F8,A5) AST_$FREE_ASTE_HEAD   (0x428,A5) AST_$AST_IN_TRANS_EC
 *   (0x468,A5) AST_$FREE_ASTES       (0x472/0x474/0x476,A5) the counters
 */

#include "ast/ast_internal.h"

void AST_$FREE_ASTE(aste_t *aste)
{
    uint16_t flags;         /* D0w */

    /* 0x00E00FCC..0x00E00FE8: btst.l #0xc (AREA), btst.l #0xb (remote) */
    flags = aste->flags;
    if (flags & ASTE_FLAG_AREA) {
        AST_$ASTE_AREA_CNT--;
    } else if (flags & ASTE_FLAG_REMOTE) {
        AST_$ASTE_R_CNT--;
    } else {
        AST_$ASTE_L_CNT--;
    }

    /* 0x00E00FEC..0x00E00FF4 */
    aste->aote = NULL;
    aste->next = AST_$FREE_ASTE_HEAD;
    AST_$FREE_ASTE_HEAD = aste;

    /* 0x00E00FF8: bset.b #0x7,(0x12,A0) - bit 15 of the flags word */
    aste->flags |= ASTE_FLAG_IN_TRANS;

    /* 0x00E00FFE */
    AST_$FREE_ASTES++;

    /* 0x00E01002..0x00E01006 */
    EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);
}

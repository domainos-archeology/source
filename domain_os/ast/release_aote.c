/*
 * ast_$release_aote - Put an AOTE back on the free list
 *
 * Clears the entry's UID to UID_$NIL, drops the remote bit and the volume
 * index, pushes it on the free list, marks it in transition (the free
 * marker), wakes AST in-transition waiters and counts it free.
 *
 * Original address: 0x00E00F7C (64 bytes).  A5 is inherited (every caller
 * is AST code; 0xE1DC80): (0x3EC,A5) free-list head, (0x428,A5)
 * AST_$AST_IN_TRANS_EC, (0x46A,A5) AST_$FREE_AOTES.  UID_$NIL is read
 * absolutely (`movea.l #0xe1737c,A0`).
 */

#include "ast/ast_internal.h"

/* The free-list cells are AST_$DATA fields, shared with ast/allocate_aote.c. */

void ast_$release_aote(aote_t *aote)
{
    /* 0x00E00F80..0x00E00F8E: two post-increment longwords into aote+0x10 */
    aote->uid.high = UID_$NIL.high;
    aote->uid.low = UID_$NIL.low;

    /* 0x00E00F92..0x00E00F98 */
    aote->remote_flag &= 0x7F;              /* bclr.b #0x7,(0xb9,A1) */
    aote->vol_index = 0;                    /* clr.b (0xb8,A1) */

    /* 0x00E00F9C..0x00E00FA0 */
    aote->hash_next = AST_$FREE_AOTE_HEAD;
    AST_$FREE_AOTE_HEAD = aote;

    /* 0x00E00FA4: bset.b #0x7,(0xbf,A1) */
    aote->flags |= AOTE_FLAG_IN_TRANS;

    /* 0x00E00FAA..0x00E00FB4 */
    EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);
    AST_$FREE_AOTES++;
}

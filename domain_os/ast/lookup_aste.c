/*
 * ast_$lookup_aste - Find the ASTE for a segment of an object
 *
 * The AOTE's ASTE list is kept in DESCENDING segment order.  The walk
 * stops with NULL as soon as it passes an entry whose segment is below
 * the one wanted.  A matching entry that is in transition (flags bit 15)
 * is waited for - with the AOTE's reference count held - and the walk
 * restarts from the list head.
 *
 * Original address: 0x00E0250C (80 bytes), A5 = 0xE1DC80 (unused here).
 * Frame: (0x8,A6) aote (A2), (0xC,A6) segment word (D2w).
 */

#include "ast/ast_internal.h"

aste_t *ast_$lookup_aste(aote_t *aote, int16_t segment)
{
    aste_t *aste;               /* A0 */

    /* 0x00E0253C: the list head */
    aste = aote->aste_list;
    while (aste != NULL) {
        /* 0x00E02524: cmp.w (0xc,A0),D2w */
        if ((uint16_t)segment == aste->segment) {
            /* 0x00E0252A: tst.w (0x12,A0) / bpl */
            if ((int16_t)aste->flags >= 0) {
                return aste;                            /* 0x00E02552 */
            }
            /* 0x00E02530..0x00E0253C: hold the AOTE while waiting, then
             * restart from the head */
            aote->ref_count++;
            AST_$WAIT_FOR_AST_INTRANS();
            aote->ref_count--;
            aste = aote->aste_list;
            continue;
        }
        /* 0x00E02542..0x00E02546: cmp.w (0xc,A0),D2w / bcc - stop once
         * the wanted segment is at or above this entry's (unsigned) */
        if ((uint16_t)segment >= aste->segment) {
            break;
        }
        /* 0x00E02548 */
        aste = aste->next;
    }

    /* 0x00E02550 */
    return NULL;
}

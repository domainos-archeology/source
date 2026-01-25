/*
 * AST_$FREE_ASTE - Free an ASTE back to the free list
 *
 * Returns an ASTE to the free list after clearing it.
 * Updates the appropriate counter based on the ASTE type.
 *
 * Original address: 0x00e00fbc
 */

#include "ast_internal.h"

void AST_$FREE_ASTE(aste_t *aste)
{
    /* Update counters based on ASTE type */
    if (aste->flags & ASTE_FLAG_AREA) {
        AST_$ASTE_AREA_CNT--;
    } else if (aste->flags & ASTE_FLAG_REMOTE) {
        AST_$ASTE_R_CNT--;
    } else {
        AST_$ASTE_L_CNT--;
    }

    /* Clear the AOTE pointer */
    aste->aote = NULL;

    /* Add to free list */
    aste->next = AST_$FREE_ASTE_HEAD;
    AST_$FREE_ASTE_HEAD = aste;

    /* Mark as free (set high bit of flags) */
    aste->flags |= ASTE_FLAG_IN_TRANS;

    /* Increment free count */
    AST_$FREE_ASTES++;

    /* Signal that an AST operation completed */
    EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);
}

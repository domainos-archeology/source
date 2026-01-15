/*
 * ast_$lookup_aste - Look up an existing ASTE for an AOTE/segment
 *
 * Searches the ASTE list for a given AOTE to find an ASTE matching
 * the requested segment number. Returns NULL if not found or if
 * segment is beyond the list.
 *
 * Original address: 0x00e0250c
 */

#include "ast/ast_internal.h"

aste_t *ast_$lookup_aste(aote_t *aote, int16_t segment)
{
    aste_t *aste;

    while (1) {
        /* Get the ASTE list head from the AOTE (at offset 0x04) */
        aste = aote->aste_list;

        /* Walk the ASTE list */
        while (aste != NULL) {
            /* Check if this ASTE matches the requested segment */
            /* Segment number is at offset 0x0C in ASTE */
            if (segment == aste->timestamp) {
                /* Found matching segment - check if in-transition */
                /* Flags at offset 0x12, bit 15 is in-transition */
                if ((int16_t)aste->flags >= 0) {
                    /* Not in transition, return it */
                    return aste;
                }
                /* In transition - wait and retry */
                /* Increment ref count at offset 0xBE of AOTE while waiting */
                aote->ref_count++;
                AST_$WAIT_FOR_AST_INTRANS();
                aote->ref_count--;
                break;  /* Start over from ASTE list head */
            }

            /* If segment list is ordered descending and we've passed it, stop */
            if (aste->timestamp <= segment) {
                return NULL;
            }

            /* Move to next ASTE in chain */
            aste = aste->next;
        }

        /* If we fell through without finding, return NULL */
        if (aste == NULL) {
            return NULL;
        }
    }
}

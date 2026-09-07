/*
 * MST_$REMOVE_SEG - Remove a segment from the Active Segment Table
 *
 * This function removes a segment's pages from the AST (Active Segment Table).
 * It is used when unmapping memory or when a segment is no longer needed.
 *
 * The function:
 * 1. Locks the AST
 * 2. Locates the AST entry for the segment
 * 3. Releases all physical pages associated with the segment
 * 4. Unlocks the AST
 */

#include "mst/mst_internal.h"

/*
 * MST_$REMOVE_SEG - Remove segment from AST
 *
 * @param request  AST_$LOCATE_ASTE request record, BY REFERENCE
 *                 (0x00E0E0EA "move.l (0x8,A6),-(SP)"; the callee
 *                 dereferences it at 0x00E0705E)
 * @param param_2  Unused in current implementation (A6+0x0C, long)
 * @param param_3  Unused in current implementation (A6+0x10, word)
 * @param param_4  Unused in current implementation (A6+0x12, word)
 * @param flags    Flags passed to AST_$RELEASE_PAGES (A6+0x14, byte)
 */
void MST_$REMOVE_SEG(locate_request_t *request, uint32_t param_2,
                      uint16_t param_3, uint16_t param_4, uint8_t flags)
{
    aste_t *aste;

    (void)param_2;  /* Unused */
    (void)param_3;  /* Unused */
    (void)param_4;  /* Unused */

    /* Lock the Active Segment Table */
    ML_$LOCK(MST_LOCK_AST);

    /* Locate the AST entry for this segment */
    aste = AST_$LOCATE_ASTE(request);

    if (aste != NULL) {
        /* Release all pages for this AST entry */
        AST_$RELEASE_PAGES(aste, flags);
    }

    /* Unlock the AST */
    ML_$UNLOCK(MST_LOCK_AST);
}

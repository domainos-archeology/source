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

#include "mst_internal.h"

/*
 * MST_$REMOVE_SEG - Remove segment from AST
 *
 * @param param_1  First parameter (passed to AST_$LOCATE_ASTE)
 * @param param_2  Unused in current implementation
 * @param param_3  Unused in current implementation
 * @param param_4  Unused in current implementation
 * @param flags    Flags passed to AST_$RELEASE_PAGES
 */
void MST_$REMOVE_SEG(uint32_t param_1, uint32_t param_2,
                      uint16_t param_3, uint16_t param_4, uint8_t flags)
{
    void *aste;

    (void)param_2;  /* Unused */
    (void)param_3;  /* Unused */
    (void)param_4;  /* Unused */

    /* Lock the Active Segment Table */
    ML_$LOCK(MST_LOCK_AST);

    /* Locate the AST entry for this segment */
    aste = AST_$LOCATE_ASTE(param_1);

    if (aste != NULL) {
        /* Release all pages for this AST entry */
        AST_$RELEASE_PAGES(aste, flags);
    }

    /* Unlock the AST */
    ML_$UNLOCK(MST_LOCK_AST);
}

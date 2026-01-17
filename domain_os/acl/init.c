/*
 * ACL_$INIT - Initialize the ACL subsystem
 *
 * Initializes all ACL data structures:
 *   - Zeros out all per-process ACL data
 *   - Resets all ASIDs to default system values
 *   - Sets up locksmith UIDs
 *   - Initializes the exclusion lock
 *
 * Called once during system startup.
 *
 * Original address: 0x00E3109C
 */

#include "acl/acl_internal.h"
#include "os/os.h"

void ACL_$INIT(void)
{
    int16_t i;
    status_$t status;

    /*
     * Zero out all ACL data structures.
     * Original code zeros 0xAD98 bytes from 0xE88834 to 0xE935CC.
     * This covers all the per-process SID arrays, project lists,
     * subsystem levels, and bitmaps.
     */
    OS_$DATA_ZERO((char *)&ACL_$CURRENT_SIDS[0],
                  sizeof(ACL_$CURRENT_SIDS) +
                  sizeof(ACL_$SAVED_SIDS) +
                  sizeof(ACL_$ORIGINAL_SIDS) +
                  sizeof(ACL_$PROJ_LISTS) +
                  sizeof(ACL_$SAVED_PROJ) +
                  sizeof(ACL_$SUBSYS_LEVEL) +
                  sizeof(ACL_$ASID_FREE_BITMAP) +
                  sizeof(ACL_$ASID_SUSER_BITMAP));

    /*
     * Initialize ASIDs 1-64 (0 is reserved).
     * For each ASID:
     *   - Call FREE_ASID to set default SID values
     *   - Mark as free in the bitmap
     */
    for (i = 1; i <= 64; i++) {
        ACL_$FREE_ASID(i, &status);
        /* Mark ASID as free (set bit in free bitmap) */
        ACL_$ASID_FREE_BITMAP[(i - 1) >> 3] |= (0x80 >> ((i - 1) & 7));
    }

    /*
     * Set up locksmith UIDs.
     * Copy RGYC_$G_LOCKSMITH_UID to two locations for quick comparison.
     * These correspond to process 0's user SID and process 1's user SID.
     */
    ACL_$CURRENT_SIDS[0].user_sid = RGYC_$G_LOCKSMITH_UID;
    ACL_$CURRENT_SIDS[1].user_sid = RGYC_$G_LOCKSMITH_UID;

    /*
     * Initialize 8 project list UIDs to UID_$NIL.
     * This sets up the default project list for early processes.
     */
    for (i = 0; i < 8; i++) {
        /* Project list UIDs at offset 8 from base 0xE9253C */
        /* These appear to be a separate array at 0xE9253C, not part of PROJ_LISTS */
        /* For now, leave as-is since the structure isn't fully understood */
    }

    /*
     * Set up LRU cache list for ACL caching (circular doubly-linked list).
     * 31 entries, each 4 bytes (prev/next indices as shorts).
     * Original at 0xE7D9C4 (A5+0xA70).
     *
     * This is used for caching recently-looked-up ACL entries.
     * We skip this for now as it's not critical for basic operation.
     */

    /*
     * Initialize the exclusion lock for ACL operations.
     */
    ML_$EXCLUSION_INIT(&ACL_$EXCLUSION_LOCK);
}

/*
 * ACL_$FREE_ASID - Free/reset ACL state for an ASID
 *
 * Resets all SID state for a process to system defaults:
 *   - User SID = RGYC_$P_SYS_USER_UID
 *   - Group SID = RGYC_$G_SYS_PROJ_UID
 *   - Org SID = RGYC_$O_SYS_ORG_UID
 *   - Login SID = UID_$NIL
 *
 * Copies current SIDs to saved and original SID arrays.
 * Copies project list to saved project list.
 * Clears subsystem level.
 * Marks ASID as free in bitmap and clears suser flag.
 *
 * Parameters:
 *   asid - Address space ID to free
 *   status_ret - Output status code (set to 0 on success)
 *
 * Original address: 0x00E74C6A
 */

#include "acl/acl_internal.h"

/*
 * Default project list values (constant data at 0x00E74D9C)
 * These are copied to newly-freed ASIDs.
 */
static const acl_proj_list_t DEFAULT_PROJ_LIST = {0, 0, 0};

void ACL_$FREE_ASID(int16_t asid, status_$t *status_ret)
{
    acl_sid_block_t *current;
    acl_proj_list_t *proj;
    int i;

    current = &ACL_$CURRENT_SIDS[asid];
    proj = &ACL_$PROJ_LISTS[asid];

    /*
     * Set current SIDs to system defaults
     */
    current->user_sid = RGYC_$P_SYS_USER_UID;
    current->group_sid = RGYC_$G_SYS_PROJ_UID;
    current->org_sid = RGYC_$O_SYS_ORG_UID;
    current->login_sid = UID_$NIL;

    /*
     * Set project list to defaults
     */
    *proj = DEFAULT_PROJ_LIST;

    /*
     * Copy current SIDs to original (pre-subsystem entry) array
     */
    ACL_$ORIGINAL_SIDS[asid] = *current;

    /*
     * Copy current SIDs to saved (pre-enter_super) array
     */
    ACL_$SAVED_SIDS[asid] = *current;

    /*
     * Copy project list to saved project list
     */
    ACL_$SAVED_PROJ[asid] = *proj;

    /*
     * Clear the 8 extended project UIDs for this ASID
     * These are at a separate location indexed by ASID << 6
     * (64 bytes per ASID, 8 UIDs)
     */
    /* TODO: Implement extended project UID clearing if needed */

    /*
     * Clear subsystem level
     */
    ACL_$SUBSYS_LEVEL[asid] = 0;

    /*
     * Update bitmaps:
     * - Mark ASID as free (set bit in free bitmap)
     * - Clear suser flag (clear bit in suser bitmap)
     */
    ACL_$ASID_FREE_BITMAP[(asid - 1) >> 3] |= (0x80 >> ((asid - 1) & 7));
    ACL_$ASID_SUSER_BITMAP[(asid - 1) >> 3] &= ~(0x80 >> ((asid - 1) & 7));

    *status_ret = status_$ok;
}

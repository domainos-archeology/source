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
 * The default 12-byte project-list cell, pushed by the `lea (0xb8,PC),A3` at
 * 0x00E74CE2 (extension word at 0x00E74CE4, so the cell is 0x00E74D9C) and
 * copied a longword at a time into ACL_$PROJ_LISTS[asid].
 *
 * Image bytes at 0x00E74D9C: 00 00 00 0d  00 00 00 0d  00 00 00 0d.
 */
static const acl_proj_list_t DEFAULT_PROJ_LIST = { 0x0000000Du, 0x0000000Du, 0x0000000Du };

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
     * 0x00E74CE0-0x00E74CEF: set the project list to the image constant.
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
     * 0x00E74D20-0x00E74D48: clear the eight project UIDs for this ASID.
     * `moveq #0x7,D1` + `dbf` is eight iterations; A0 = 0xE97294 + asid*0x40
     * and D2 starts at 8, so the addresses written are
     * 0xE924F4 + asid*0x40 + 8 + i*8 = &ACL_$PROJ_UIDS[asid][i] with the
     * 0xE924FC base recorded in acl/acl_internal.h (source-4h7g).
     */
    /* TODO(source-x5dd): the routine indexes the SID and project tables with
     * `asid` but the two bitmaps with (asid-1); confirm ASID == PID here. */
    for (i = 0; i <= 7; i++) {
        ACL_$PROJ_UIDS[asid][i] = UID_$NIL;
    }

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

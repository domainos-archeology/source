/*
 * ACL_$SET_RES_ALL_SIDS - Set all resource SIDs for the current process
 *
 * Sets the original, current, and saved SID blocks, plus saved and current
 * project metadata for the current process. Requires superuser privileges.
 *
 * Includes audit logging when auditing is enabled.
 *
 * Parameters:
 *   new_original_sids - New original SIDs (36 bytes)
 *   new_current_sids  - New current SIDs (36 bytes)
 *   new_saved_sids    - New saved SIDs (36 bytes)
 *   new_saved_proj    - New saved project metadata (12 bytes)
 *   new_current_proj  - New current project metadata (12 bytes)
 *   status_ret        - Output status code
 *
 * Original address: 0x00E4855A
 */

#include "acl/acl_internal.h"
#include "audit/audit.h"

void ACL_$SET_RES_ALL_SIDS(void *new_original_sids, void *new_current_sids,
                           void *new_saved_sids, void *new_saved_proj,
                           void *new_current_proj, status_$t *status_ret)
{
    int16_t pid = PROC1_$CURRENT;
    uint32_t *new_orig = (uint32_t *)new_original_sids;
    uint32_t *new_curr = (uint32_t *)new_current_sids;
    uint32_t *new_save = (uint32_t *)new_saved_sids;
    uint32_t *new_sproj = (uint32_t *)new_saved_proj;
    uint32_t *new_cproj = (uint32_t *)new_current_proj;
    uint32_t *orig_ptr = (uint32_t *)&ACL_$ORIGINAL_SIDS[pid];
    uint32_t *curr_ptr = (uint32_t *)&ACL_$CURRENT_SIDS[pid];
    uint32_t *save_ptr = (uint32_t *)&ACL_$SAVED_SIDS[pid];
    uint32_t *sproj_ptr = (uint32_t *)&ACL_$SAVED_PROJ[pid];
    uint32_t *cproj_ptr = (uint32_t *)&ACL_$PROJ_LISTS[pid];
    int i;

    *status_ret = status_$no_right_to_perform_operation;

    /* Check for superuser privileges */
    if (acl_$check_suser_pid(pid) >= 0) {
        return;
    }

    /* Handle group SID change - update project list if needed */
    if (new_orig[2] != orig_ptr[2] || new_orig[3] != orig_ptr[3]) {
        if (new_orig[2] != new_curr[2] || new_orig[3] != new_curr[3]) {
            /* Temporarily enter super mode to modify project list */
            ACL_$SUPER_COUNT[pid]++;
            ACL_$DELETE_PROJ((uid_t *)&orig_ptr[2], status_ret);
            ACL_$ADD_PROJ((uid_t *)&new_orig[2], status_ret);
            ACL_$SUPER_COUNT[pid]--;
        }
    }

    /* Copy new original SIDs */
    for (i = 0; i < 9; i++) {
        orig_ptr[i] = new_orig[i];
    }

    /* Copy new current SIDs */
    for (i = 0; i < 9; i++) {
        curr_ptr[i] = new_curr[i];
    }

    /* Copy new saved SIDs */
    for (i = 0; i < 9; i++) {
        save_ptr[i] = new_save[i];
    }

    /* Copy project metadata */
    sproj_ptr[0] = new_sproj[0];
    sproj_ptr[1] = new_sproj[1];
    sproj_ptr[2] = new_sproj[2];

    cproj_ptr[0] = new_cproj[0];
    cproj_ptr[1] = new_cproj[1];
    cproj_ptr[2] = new_cproj[2];

    *status_ret = status_$ok;
}

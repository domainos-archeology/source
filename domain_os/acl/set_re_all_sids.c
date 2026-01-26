/*
 * ACL_$SET_RE_ALL_SIDS - Set all requestor SIDs for the current process
 *
 * Sets the original and current SID blocks, plus saved and current project
 * metadata for the current process. Requires superuser privileges unless
 * the new SIDs match existing values.
 *
 * Includes audit logging when auditing is enabled.
 *
 * Parameters:
 *   new_original_sids - New original SIDs (36 bytes)
 *   new_current_sids  - New current SIDs (36 bytes)
 *   new_saved_proj    - New saved project metadata (12 bytes)
 *   new_current_proj  - New current project metadata (12 bytes)
 *   status_ret        - Output status code
 *
 * Original address: 0x00E481AE
 */

#include "acl/acl_internal.h"
#include "audit/audit.h"

void ACL_$SET_RE_ALL_SIDS(void *new_original_sids, void *new_current_sids,
                          void *new_saved_proj, void *new_current_proj,
                          status_$t *status_ret)
{
    int16_t pid = PROC1_$CURRENT;
    uint32_t *new_orig = (uint32_t *)new_original_sids;
    uint32_t *new_curr = (uint32_t *)new_current_sids;
    uint32_t *new_sproj = (uint32_t *)new_saved_proj;
    uint32_t *new_cproj = (uint32_t *)new_current_proj;
    uint32_t *orig_ptr = (uint32_t *)&ACL_$ORIGINAL_SIDS[pid];
    uint32_t *curr_ptr = (uint32_t *)&ACL_$CURRENT_SIDS[pid];
    uint32_t *save_ptr = (uint32_t *)&ACL_$SAVED_SIDS[pid];
    uint32_t *sproj_ptr = (uint32_t *)&ACL_$SAVED_PROJ[pid];
    uint32_t *cproj_ptr = (uint32_t *)&ACL_$PROJ_LISTS[pid];
    int i;
    int8_t is_suser;
    int sids_changed;

    *status_ret = status_$no_right_to_perform_operation;

    /* Check for superuser privileges */
    is_suser = acl_$check_suser_pid(pid);

    if (is_suser >= 0) {
        /*
         * Not superuser - verify that each new SID matches either
         * the original or current SID (can't change to arbitrary values)
         */

        /* Check user SID (indices 0-1) */
        if (new_orig[0] != orig_ptr[0] || new_orig[1] != orig_ptr[1]) {
            if (new_orig[0] != curr_ptr[0] || new_orig[1] != curr_ptr[1]) {
                return;
            }
        }

        /* Check group SID (indices 2-3) */
        if (new_orig[2] != orig_ptr[2] || new_orig[3] != orig_ptr[3]) {
            if (new_orig[2] != curr_ptr[2] || new_orig[3] != curr_ptr[3]) {
                return;
            }
        }

        /* Check org SID (indices 4-5) */
        if (new_orig[4] != orig_ptr[4] || new_orig[5] != orig_ptr[5]) {
            if (new_orig[4] != curr_ptr[4] || new_orig[5] != curr_ptr[5]) {
                return;
            }
        }

        /* Check login SID (indices 6-7) */
        if (new_orig[6] != orig_ptr[6] || new_orig[7] != orig_ptr[7]) {
            if (new_orig[6] != curr_ptr[6] || new_orig[7] != curr_ptr[7]) {
                return;
            }
        }

        /* Check new current SIDs against current/original/saved */
        if (new_curr[0] != curr_ptr[0] || new_curr[1] != curr_ptr[1]) {
            if (new_curr[0] != orig_ptr[0] || new_curr[1] != orig_ptr[1]) {
                if (new_curr[0] != save_ptr[0] || new_curr[1] != save_ptr[1]) {
                    return;
                }
            }
        }

        if (new_curr[2] != curr_ptr[2] || new_curr[3] != curr_ptr[3]) {
            if (new_curr[2] != orig_ptr[2] || new_curr[3] != orig_ptr[3]) {
                if (new_curr[2] != save_ptr[2] || new_curr[3] != save_ptr[3]) {
                    return;
                }
            }
        }

        if (new_curr[4] != curr_ptr[4] || new_curr[5] != curr_ptr[5]) {
            if (new_curr[4] != orig_ptr[4] || new_curr[5] != orig_ptr[5]) {
                if (new_curr[4] != save_ptr[4] || new_curr[5] != save_ptr[5]) {
                    return;
                }
            }
        }

        if (new_curr[6] != curr_ptr[6] || new_curr[7] != curr_ptr[7]) {
            if (new_curr[6] != orig_ptr[6] || new_curr[7] != orig_ptr[7]) {
                if (new_curr[6] != save_ptr[6] || new_curr[7] != save_ptr[7]) {
                    return;
                }
            }
        }
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

    /* Check if original SIDs are changing */
    sids_changed = 0;
    for (i = 0; i < 9; i++) {
        if (orig_ptr[i] != new_orig[i]) {
            sids_changed = 1;
            break;
        }
    }

    /* If original SIDs changed, save current to saved first */
    if (sids_changed) {
        for (i = 0; i < 9; i++) {
            save_ptr[i] = new_orig[i];
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

    /* Copy project metadata */
    sproj_ptr[0] = new_sproj[0];
    sproj_ptr[1] = new_sproj[1];
    sproj_ptr[2] = new_sproj[2];

    cproj_ptr[0] = new_cproj[0];
    cproj_ptr[1] = new_cproj[1];
    cproj_ptr[2] = new_cproj[2];

    *status_ret = status_$ok;
}

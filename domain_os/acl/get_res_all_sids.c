/*
 * ACL_$GET_RES_ALL_SIDS - Get all resource SIDs and project lists for the current process
 *
 * Returns the original, current, and saved SID blocks, plus saved and current
 * project list metadata for the current process.
 *
 * Parameters:
 *   original_sids  - Output buffer for original SIDs (36 bytes)
 *   current_sids   - Output buffer for current SIDs (36 bytes)
 *   saved_sids     - Output buffer for saved SIDs (36 bytes)
 *   saved_proj     - Output buffer for saved project metadata (12 bytes)
 *   current_proj   - Output buffer for current project metadata (12 bytes)
 *   status_ret     - Output status code
 *
 * Original address: 0x00E4881C
 */

#include "acl/acl_internal.h"

void ACL_$GET_RES_ALL_SIDS(void *original_sids, void *current_sids, void *saved_sids,
                           void *saved_proj, void *current_proj, status_$t *status_ret)
{
    int16_t pid = PROC1_$CURRENT;
    uint32_t *orig_out = (uint32_t *)original_sids;
    uint32_t *curr_out = (uint32_t *)current_sids;
    uint32_t *save_out = (uint32_t *)saved_sids;
    uint32_t *sproj_out = (uint32_t *)saved_proj;
    uint32_t *cproj_out = (uint32_t *)current_proj;
    uint32_t *orig_src = (uint32_t *)&ACL_$ORIGINAL_SIDS[pid];
    uint32_t *curr_src = (uint32_t *)&ACL_$CURRENT_SIDS[pid];
    uint32_t *save_src = (uint32_t *)&ACL_$SAVED_SIDS[pid];
    uint32_t *sproj_src = (uint32_t *)&ACL_$SAVED_PROJ[pid];
    uint32_t *cproj_src = (uint32_t *)&ACL_$PROJ_LISTS[pid];
    int i;

    /* Copy 9 uint32_t values (36 bytes) for each SID block */
    for (i = 0; i < 9; i++) {
        orig_out[i] = orig_src[i];
    }

    for (i = 0; i < 9; i++) {
        curr_out[i] = curr_src[i];
    }

    for (i = 0; i < 9; i++) {
        save_out[i] = save_src[i];
    }

    /* Copy 3 uint32_t values (12 bytes) for project metadata */
    sproj_out[0] = sproj_src[0];
    sproj_out[1] = sproj_src[1];
    sproj_out[2] = sproj_src[2];

    cproj_out[0] = cproj_src[0];
    cproj_out[1] = cproj_src[1];
    cproj_out[2] = cproj_src[2];

    *status_ret = status_$ok;
}

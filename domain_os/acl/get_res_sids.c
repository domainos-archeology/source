/*
 * ACL_$GET_RES_SIDS - Get resource SIDs for the current process
 *
 * Returns the original, current, and saved SID blocks for the current process.
 *
 * Parameters:
 *   original_sids - Output buffer for original SIDs (36 bytes)
 *   current_sids  - Output buffer for current SIDs (36 bytes)
 *   saved_sids    - Output buffer for saved SIDs (36 bytes)
 *   status_ret    - Output status code
 *
 * Original address: 0x00E4890C
 */

#include "acl/acl_internal.h"

void ACL_$GET_RES_SIDS(void *original_sids, void *current_sids, void *saved_sids,
                       status_$t *status_ret)
{
    int16_t pid = PROC1_$CURRENT;
    uint32_t *orig_out = (uint32_t *)original_sids;
    uint32_t *curr_out = (uint32_t *)current_sids;
    uint32_t *save_out = (uint32_t *)saved_sids;
    uint32_t *orig_src = (uint32_t *)&ACL_$ORIGINAL_SIDS[pid];
    uint32_t *curr_src = (uint32_t *)&ACL_$CURRENT_SIDS[pid];
    uint32_t *save_src = (uint32_t *)&ACL_$SAVED_SIDS[pid];
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

    *status_ret = status_$ok;
}

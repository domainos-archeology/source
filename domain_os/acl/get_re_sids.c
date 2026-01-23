/*
 * ACL_$GET_RE_SIDS - Get requestor SIDs for the current process
 *
 * Returns the original and current SID blocks for the current process.
 *
 * Parameters:
 *   original_sids - Output buffer for original SIDs (36 bytes)
 *   current_sids  - Output buffer for current SIDs (36 bytes)
 *   status_ret    - Output status code
 *
 * Original address: 0x00E488B6
 */

#include "acl/acl_internal.h"

void ACL_$GET_RE_SIDS(void *original_sids, void *current_sids, status_$t *status_ret)
{
    int16_t pid = PROC1_$CURRENT;
    uint32_t *orig_out = (uint32_t *)original_sids;
    uint32_t *curr_out = (uint32_t *)current_sids;
    uint32_t *orig_src = (uint32_t *)&ACL_$ORIGINAL_SIDS[pid];
    uint32_t *curr_src = (uint32_t *)&ACL_$CURRENT_SIDS[pid];
    int i;

    /* Copy 9 uint32_t values (36 bytes) for original SIDs */
    for (i = 0; i < 9; i++) {
        orig_out[i] = orig_src[i];
    }

    /* Copy 9 uint32_t values (36 bytes) for current SIDs */
    for (i = 0; i < 9; i++) {
        curr_out[i] = curr_src[i];
    }

    *status_ret = status_$ok;
}

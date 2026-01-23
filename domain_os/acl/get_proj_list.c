/*
 * ACL_$GET_PROJ_LIST - Get the project list for the current process
 *
 * Retrieves the project UIDs from the current process's project list.
 *
 * Parameters:
 *   proj_acls  - Output buffer for project UIDs
 *   max_count  - Pointer to maximum number of projects to retrieve (clamped to 8)
 *   count_ret  - Output: actual number of projects retrieved
 *   status_ret - Output status code
 *
 * Original address: 0x00E48034
 */

#include "acl/acl_internal.h"

void ACL_$GET_PROJ_LIST(uid_t *proj_acls, int16_t *max_count, int16_t *count_ret,
                        status_$t *status_ret)
{
    int16_t pid = PROC1_$CURRENT;
    int16_t max = *max_count;
    int16_t count = 0;
    int i;

    *status_ret = status_$ok;

    /* Clamp max to 8 */
    if (max > ACL_MAX_PROJECTS) {
        max = ACL_MAX_PROJECTS;
    }

    /* Copy valid project UIDs to output buffer */
    for (i = 0; i < max; i++) {
        /* Check for empty slot (UID_$NIL) - end of list */
        if (ACL_$PROJ_UIDS[pid][i].high == UID_$NIL.high &&
            ACL_$PROJ_UIDS[pid][i].low == UID_$NIL.low) {
            break;
        }

        /* Copy the UID */
        proj_acls[count].high = ACL_$PROJ_UIDS[pid][i].high;
        proj_acls[count].low = ACL_$PROJ_UIDS[pid][i].low;
        count++;
    }

    /* Fill remaining slots with UID_$NIL */
    for (i = count; i < max; i++) {
        proj_acls[i].high = UID_$NIL.high;
        proj_acls[i].low = UID_$NIL.low;
    }

    *count_ret = count;
}

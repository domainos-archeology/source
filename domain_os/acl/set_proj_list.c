/*
 * ACL_$SET_PROJ_LIST - Set the project list for the current process
 *
 * Sets the project UIDs in the current process's project list.
 * Requires superuser privileges.
 *
 * Parameters:
 *   proj_acls  - Array of project UIDs to set
 *   count      - Pointer to number of projects to set (max 8)
 *   status_ret - Output status code
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$no_right_to_perform_operation - Not superuser
 *   status_$acl_proj_list_too_big - count > 8
 *
 * Original address: 0x00E480F4
 */

#include "acl/acl_internal.h"

void ACL_$SET_PROJ_LIST(uid_t *proj_acls, int16_t *count, status_$t *status_ret)
{
    int16_t pid = PROC1_$CURRENT;
    int16_t num = *count;
    int i;

    /* Check for superuser privileges */
    if (acl_$check_suser_pid(pid) >= 0) {
        *status_ret = status_$no_right_to_perform_operation;
        return;
    }

    /* Clamp negative to 0 */
    if (num < 0) {
        num = 0;
    }

    /* Check for too many projects */
    if (num > ACL_MAX_PROJECTS) {
        *status_ret = status_$acl_proj_list_too_big;
        return;
    }

    *status_ret = status_$ok;

    /* Copy the provided project UIDs */
    for (i = 0; i < num; i++) {
        ACL_$PROJ_UIDS[pid][i].high = proj_acls[i].high;
        ACL_$PROJ_UIDS[pid][i].low = proj_acls[i].low;
    }

    /* Clear remaining slots with UID_$NIL */
    for (i = num; i < ACL_MAX_PROJECTS; i++) {
        ACL_$PROJ_UIDS[pid][i].high = UID_$NIL.high;
        ACL_$PROJ_UIDS[pid][i].low = UID_$NIL.low;
    }
}

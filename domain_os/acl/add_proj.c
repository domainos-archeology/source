/*
 * ACL_$ADD_PROJ - Add a project to the current process's project list
 *
 * Adds the specified project UID to the project list for the current process.
 * Requires superuser privileges.
 *
 * The project list holds up to 8 UIDs. If the project is already in the list,
 * the function returns success without adding a duplicate. If the list is full,
 * returns status_$project_list_is_full.
 *
 * Original address: 0x00E47EAC
 */

#include "acl/acl_internal.h"

void ACL_$ADD_PROJ(uid_t *proj_acl, status_$t *status_ret)
{
    int16_t pid = PROC1_$CURRENT;
    int i;

    /* Check for superuser privileges */
    if (acl_$check_suser_pid(pid) >= 0) {
        *status_ret = status_$no_right_to_perform_operation;
        return;
    }

    *status_ret = status_$ok;

    /* Search for existing entry or empty slot */
    for (i = 0; i < ACL_MAX_PROJECTS; i++) {
        /* Check if this project is already in the list */
        if (ACL_$PROJ_UIDS[pid][i].high == proj_acl->high &&
            ACL_$PROJ_UIDS[pid][i].low == proj_acl->low) {
            /* Already exists, return success */
            return;
        }

        /* Check for empty slot (UID_$NIL) */
        if (ACL_$PROJ_UIDS[pid][i].high == UID_$NIL.high &&
            ACL_$PROJ_UIDS[pid][i].low == UID_$NIL.low) {
            /* Found empty slot, add the project */
            ACL_$PROJ_UIDS[pid][i].high = proj_acl->high;
            ACL_$PROJ_UIDS[pid][i].low = proj_acl->low;
            return;
        }
    }

    /* List is full */
    *status_ret = status_$project_list_is_full;
}

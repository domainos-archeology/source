/*
 * ACL_$DELETE_PROJ - Delete a project from the current process's project list
 *
 * Removes the specified project UID from the project list for the current process.
 * Requires superuser privileges.
 *
 * If the project is not found, the function returns success without modification.
 * After deletion, the list is compacted by shifting subsequent entries up.
 *
 * Original address: 0x00E47F54
 */

#include "acl/acl_internal.h"

void ACL_$DELETE_PROJ(uid_t *proj_acl, status_$t *status_ret)
{
    int16_t pid = PROC1_$CURRENT;
    int i, j;

    /* Check for superuser privileges */
    if (acl_$check_suser_pid(pid) >= 0) {
        *status_ret = status_$no_right_to_perform_operation;
        return;
    }

    *status_ret = status_$ok;

    /* Search for the project in the list */
    for (i = 0; i < ACL_MAX_PROJECTS; i++) {
        /* Check for empty slot (UID_$NIL) - end of list */
        if (ACL_$PROJ_UIDS[pid][i].high == UID_$NIL.high &&
            ACL_$PROJ_UIDS[pid][i].low == UID_$NIL.low) {
            /* Reached end of list, project not found */
            return;
        }

        /* Check if this is the project to delete */
        if (ACL_$PROJ_UIDS[pid][i].high == proj_acl->high &&
            ACL_$PROJ_UIDS[pid][i].low == proj_acl->low) {
            /* Found it - compact the list by shifting subsequent entries */
            for (j = i; j < ACL_MAX_PROJECTS - 1; j++) {
                ACL_$PROJ_UIDS[pid][j].high = ACL_$PROJ_UIDS[pid][j + 1].high;
                ACL_$PROJ_UIDS[pid][j].low = ACL_$PROJ_UIDS[pid][j + 1].low;

                /* Stop if we just copied a NIL entry */
                if (ACL_$PROJ_UIDS[pid][j].high == UID_$NIL.high &&
                    ACL_$PROJ_UIDS[pid][j].low == UID_$NIL.low) {
                    break;
                }
            }

            /* Clear the last slot (or the slot after the last copied entry) */
            ACL_$PROJ_UIDS[pid][ACL_MAX_PROJECTS - 1].high = UID_$NIL.high;
            ACL_$PROJ_UIDS[pid][ACL_MAX_PROJECTS - 1].low = UID_$NIL.low;
            return;
        }
    }
}

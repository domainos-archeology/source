/*
 * ACL_$CHECK_FAULT_RIGHTS - Check fault handling rights between processes
 *
 * Determines if process pid1 has rights to handle faults for process pid2.
 * This is used for debugger/fault handler privilege checking.
 *
 * A process can handle faults for another if:
 *   - pid1 is superuser
 *   - pid1 is a special process type
 *   - The SIDs match between the processes in certain ways:
 *     - pid1's original SID matches pid2's saved SID
 *     - pid1's original SID matches pid2's current SID
 *     - pid1's current SID matches pid2's current SID
 *     - pid1's current SID matches pid2's saved SID
 *
 * Parameters:
 *   pid1 - Pointer to first process ID (fault handler)
 *   pid2 - Pointer to second process ID (faulting process)
 *
 * Returns:
 *   Non-zero (-1) if fault handling is allowed, 0 otherwise
 *
 * Original address: 0x00E48A28
 */

#include "acl/acl_internal.h"

/*
 * Helper to compare two UIDs
 */
static int8_t uid_eq(uid_t *a, uid_t *b)
{
    return (a->high == b->high && a->low == b->low) ? -1 : 0;
}

int8_t ACL_$CHECK_FAULT_RIGHTS(int16_t *pid1_ptr, int16_t *pid2_ptr)
{
    int16_t pid1 = *pid1_ptr;
    int16_t pid2 = *pid2_ptr;
    int8_t allowed = 0;
    acl_sid_block_t *p1_orig, *p1_curr;
    acl_sid_block_t *p2_saved, *p2_curr;

    /* Check if pid1 has superuser privilege */
    if (acl_$check_suser_pid(pid1) < 0) {
        allowed = -1;
    }

    /* Check if pid1 is a special process type */
    if (acl_$is_process_type_2(pid1) < 0) {
        allowed = -1;
    }

    /* Get SID blocks for both processes */
    p1_orig = &ACL_$ORIGINAL_SIDS[pid1];
    p1_curr = &ACL_$CURRENT_SIDS[pid1];
    p2_saved = &ACL_$SAVED_SIDS[pid2];
    p2_curr = &ACL_$CURRENT_SIDS[pid2];

    /*
     * Check if SIDs match in any allowed combination:
     * 1. pid1 original == pid2 saved
     * 2. pid1 original == pid2 current
     * 3. pid1 current == pid2 current
     * 4. pid1 current == pid2 saved
     */
    if (uid_eq(&p1_orig->user_sid, &p2_saved->user_sid) ||
        uid_eq(&p1_orig->user_sid, &p2_curr->user_sid) ||
        uid_eq(&p1_curr->user_sid, &p2_curr->user_sid) ||
        uid_eq(&p1_curr->user_sid, &p2_saved->user_sid)) {
        allowed = -1;
    }

    return allowed;
}

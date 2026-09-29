/*
 * ACL_$CLEAR_SUPER - Clear superuser mode for current process
 *
 * Clears the superuser mode counter to zero and releases any held
 * locksmith override if owned by this process.
 *
 * Called during process cleanup or when explicitly dropping privileges.
 *
 * Original address: 0x00E46FF8
 */

#include "acl/acl_internal.h"

void ACL_$CLEAR_SUPER(void)
{
    /* Clear the super mode counter */
    ACL_$UNWIRED_DATA.super_count[PROC1_$CURRENT] = 0;

    /* If we hold the locksmith override, release it */
    if (ACL_$UNWIRED_DATA.locksmith_override < 0 &&
        PROC1_$CURRENT == ACL_$UNWIRED_DATA.locksmith_owner_pid) {
        ML_$EXCLUSION_STOP(&ACL_$WIRED_DATA.exclusion_lock);
        ACL_$UNWIRED_DATA.locksmith_override = 0;
    }
}

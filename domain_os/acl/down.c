/*
 * ACL_$DOWN - Decrement subsystem level for current process
 *
 * Decrements the subsystem level counter for the current process,
 * but not below zero. Called when exiting a privileged subsystem context.
 *
 * Original address: 0x00E47068
 */

#include "acl/acl_internal.h"

void ACL_$DOWN(void)
{
    if (ACL_$SUBSYS_LEVEL[PROC1_$CURRENT] > 0) {
        ACL_$SUBSYS_LEVEL[PROC1_$CURRENT]--;
    }
}

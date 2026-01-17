/*
 * ACL_$UP - Increment subsystem level for current process
 *
 * Increments the subsystem level counter for the current process.
 * Called when entering a privileged subsystem context.
 *
 * Original address: 0x00E4703E
 */

#include "acl/acl_internal.h"

void ACL_$UP(void)
{
    ACL_$SUBSYS_LEVEL[PROC1_$CURRENT]++;
}

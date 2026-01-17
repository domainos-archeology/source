/*
 * ACL_$IN_SUBSYS - Check if current process is in subsystem context
 *
 * Returns non-zero if the current process is executing within a
 * subsystem context (subsystem level > 0).
 *
 * Original address: 0x00E47098
 */

#include "acl/acl_internal.h"

int16_t ACL_$IN_SUBSYS(void)
{
    return (ACL_$SUBSYS_LEVEL[PROC1_$CURRENT] > 0) ? -1 : 0;
}

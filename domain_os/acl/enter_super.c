/*
 * ACL_$ENTER_SUPER - Enter superuser mode for current process
 *
 * Increments the superuser mode counter for the current process.
 * While in super mode, the process bypasses certain permission checks.
 *
 * Must be balanced with a corresponding ACL_$EXIT_SUPER call.
 *
 * Original address: 0x00E46F90
 */

#include "acl/acl_internal.h"

void ACL_$ENTER_SUPER(void)
{
    ACL_$SUPER_COUNT[PROC1_$CURRENT]++;
}

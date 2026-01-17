/*
 * ACL_$IS_SUSER - Check if current process has superuser privileges
 *
 * Returns non-zero if the current process has superuser (root) privileges.
 * This is a wrapper that calls the internal check function with the
 * current process ID.
 *
 * Original address: 0x00E47E8C
 */

#include "acl/acl_internal.h"

int8_t ACL_$IS_SUSER(void)
{
    return acl_$check_suser_pid(PROC1_$CURRENT);
}

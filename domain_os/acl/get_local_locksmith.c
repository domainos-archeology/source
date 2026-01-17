/*
 * ACL_$GET_LOCAL_LOCKSMITH - Check if local locksmith mode is enabled
 *
 * Returns the locksmith status for the local node.
 *
 * Returns:
 *   0 if locksmith mode is enabled (caller has locksmith privileges)
 *   Non-zero otherwise
 *
 * Original address: 0x00E4923C
 */

#include "acl/acl_internal.h"

int16_t ACL_$GET_LOCAL_LOCKSMITH(void)
{
    return ACL_$LOCAL_LOCKSMITH;
}

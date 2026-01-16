/*
 * NAME_$SET_ACL - Set ACL on a named object
 *
 * Simple wrapper that delegates to DIR_$SET_ACL.
 *
 * Original address: 0x00e58656
 * Original size: 26 bytes
 */

#include "name/name_internal.h"

/* Forward declaration for DIR subsystem */
extern void DIR_$SET_ACL(uid_t *uid, void *acl, status_$t *status_ret);

/*
 * NAME_$SET_ACL - Set ACL on a named object
 *
 * Sets the access control list for an object identified by its UID.
 *
 * Parameters:
 *   uid        - UID of the object
 *   acl        - ACL data to apply
 *   status_ret - Output: status code
 */
void NAME_$SET_ACL(uid_t *uid, void *acl, status_$t *status_ret)
{
    DIR_$SET_ACL(uid, acl, status_ret);
}

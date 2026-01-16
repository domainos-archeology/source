/*
 * ACL - Access Control List Management
 *
 * This module provides access control list operations.
 */

#ifndef ACL_H
#define ACL_H

#include "base/base.h"

/*
 * ACL_$GET_EXSID - Get extended SID for current process
 *
 * @param exsid   Output buffer for extended SID
 * @param status  Output status code
 */
void ACL_$GET_EXSID(void *exsid, status_$t *status);

/*
 * ACL_$IS_SUSER - Check if current process has superuser privileges
 *
 * Returns non-zero if the current process has superuser (root) privileges.
 *
 * Returns:
 *   Non-zero if superuser, 0 otherwise
 *
 * Original address: TBD
 */
int8_t ACL_$IS_SUSER(void);

/*
 * ACL_$RIGHTS - Check access rights for an object
 *
 * Verifies the caller has the specified access rights to an object.
 *
 * Parameters:
 *   uid           - UID of object to check
 *   unused        - Unused parameter (pass 0)
 *   required_mask - Pointer to required access rights mask
 *   option_flags  - Pointer to option flags
 *   status        - Output status code
 *
 * Returns:
 *   Non-zero if access granted, 0 if denied
 *
 * Original address: 0x00E46A00
 */
int16_t ACL_$RIGHTS(uid_t *uid, void *unused, uint32_t *required_mask,
                    int16_t *option_flags, status_$t *status);

#endif /* ACL_H */

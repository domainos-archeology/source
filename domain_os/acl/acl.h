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

#endif /* ACL_H */

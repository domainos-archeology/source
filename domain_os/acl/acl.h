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

#endif /* ACL_H */

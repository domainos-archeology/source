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

/*
 * ACL_$SET_ACL_CHECK - Check ACL permissions for set operation
 *
 * Verifies the caller has permission to modify ACL on a file.
 *
 * Parameters:
 *   file_uid         - UID of file to modify
 *   acl_data         - ACL data buffer
 *   source_uid       - Source ACL UID (or NULL)
 *   prot_type        - Pointer to protection type
 *   permission_flags - Output permission flags
 *   status_ret       - Output status code
 *
 * Returns:
 *   0 or positive if check completed successfully
 *   Negative if check could not be performed
 *
 * Original address: 0x00E470C4
 */
int8_t ACL_$SET_ACL_CHECK(uid_t *file_uid, void *acl_data, uid_t *source_uid,
                          int16_t *prot_type, int8_t *permission_flags,
                          status_$t *status_ret);

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
int16_t ACL_$GET_LOCAL_LOCKSMITH(void);

/*
 * ACL_$CONVERT_FUNKY_ACL - Convert "funky" ACL format
 *
 * Converts an ACL UID in the "funky" format (with type encoded in low word)
 * to standard ACL components.
 *
 * Parameters:
 *   acl_uid        - ACL UID in funky format
 *   acl_data_out   - Output ACL data buffer (48 bytes)
 *   prot_info_out  - Output protection info (8 bytes)
 *   target_uid_out - Output target ACL UID (8 bytes)
 *   status_ret     - Output status code
 *
 * Original address: 0x00E4900C
 */
void ACL_$CONVERT_FUNKY_ACL(void *acl_uid, void *acl_data_out,
                             void *prot_info_out, void *target_uid_out,
                             status_$t *status_ret);

/*
 * ACL_$DEF_ACLDATA - Get default ACL data
 *
 * Fills in default ACL data using nil user/group/org UIDs.
 *
 * Parameters:
 *   acl_data_out - Output ACL data buffer (44 bytes)
 *   uid_out      - Output UID buffer (8 bytes, set to UID_$NIL)
 *
 * Original address: 0x00E478DC
 */
void ACL_$DEF_ACLDATA(void *acl_data_out, void *uid_out);

/*
 * ACL_$GET_RE_ALL_SIDS - Get all SIDs for current requester
 *
 * Gets the owner UID and group/org UIDs for the current process context.
 * Used during file creation to determine default ownership.
 *
 * Parameters:
 *   acl_data   - Output buffer for ACL data (40 bytes)
 *   owner_uid  - Output owner UID (8 bytes)
 *   prot_info  - Output protection info (16 bytes)
 *   result     - Output result array (3 int32_t values):
 *                result[0]: owner SID status (0xC = no SID)
 *                result[1]: group SID status
 *                result[2]: org SID status
 *   status     - Output status code
 *
 * Returns:
 *   Result value (typically 0)
 *
 * Original address: 0x00E48792
 */
uint32_t ACL_$GET_RE_ALL_SIDS(void *acl_data, uid_t *owner_uid,
                              void *prot_info, int32_t *result,
                              status_$t *status);

/*
 * Default ACL UIDs for different object types
 * These are used during file creation to set default protections.
 */
extern uid_t ACL_$DNDCAL;   /* 0xE174DC: Default ACL for dirs/links */
extern uid_t ACL_$FNDWRX;   /* 0xE174C4: Default ACL for files */

#endif /* ACL_H */

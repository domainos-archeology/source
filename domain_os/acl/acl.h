/*
 * ACL - Access Control List Management
 *
 * This module provides access control list operations including:
 * - Permission checking (rights verification)
 * - Subsystem privilege management (UP/DOWN, ENTER/EXIT_SUPER)
 * - Process SID (Security ID) management
 * - Superuser status checking
 * - Locksmith mode support
 *
 * The ACL subsystem maintains per-process security context including:
 * - User, Group, Organization, and Login SIDs
 * - Subsystem nesting level
 * - Superuser mode counter
 */

#ifndef ACL_H
#define ACL_H

#include "base/base.h"

/*
 * ============================================================================
 * Initialization
 * ============================================================================
 */

/*
 * ACL_$INIT - Initialize the ACL subsystem
 *
 * Must be called once during system startup before any other ACL functions.
 *
 * Original address: 0x00E3109C
 */
void ACL_$INIT(void);

/*
 * ============================================================================
 * Superuser Status
 * ============================================================================
 */

/*
 * ACL_$IS_SUSER - Check if current process has superuser privileges
 *
 * A process has superuser status if any of:
 *   - It is PID 1 (init process)
 *   - It is in super mode (ACL_$ENTER_SUPER called)
 *   - Its login UID matches the login UID
 *   - Any of its SIDs match the locksmith UID
 *
 * Returns:
 *   Non-zero (-1) if superuser, 0 otherwise
 *
 * Original address: 0x00E47E8C
 */
int8_t ACL_$IS_SUSER(void);

/*
 * ACL_$GET_LOCAL_LOCKSMITH - Check if local locksmith mode is enabled
 *
 * Returns the locksmith status for the local node.
 *
 * Returns:
 *   0 if locksmith mode is enabled
 *   Non-zero otherwise
 *
 * Original address: 0x00E4923C
 */
int16_t ACL_$GET_LOCAL_LOCKSMITH(void);

/*
 * ============================================================================
 * Subsystem Privilege Management
 * ============================================================================
 */

/*
 * ACL_$UP - Increment subsystem level
 *
 * Increments the subsystem nesting level for the current process.
 * While in a subsystem context, additional privileges may be granted.
 *
 * Original address: 0x00E4703E
 */
void ACL_$UP(void);

/*
 * ACL_$DOWN - Decrement subsystem level
 *
 * Decrements the subsystem nesting level for the current process.
 * Will not decrement below zero.
 *
 * Original address: 0x00E47068
 */
void ACL_$DOWN(void);

/*
 * ACL_$IN_SUBSYS - Check if in subsystem context
 *
 * Returns:
 *   Non-zero (-1) if subsystem level > 0, 0 otherwise
 *
 * Original address: 0x00E47098
 */
int16_t ACL_$IN_SUBSYS(void);

/*
 * ACL_$ENTER_SUPER - Enter superuser mode
 *
 * Increments the superuser mode counter. While in super mode,
 * the process bypasses certain permission checks.
 * Must be balanced with ACL_$EXIT_SUPER.
 *
 * Original address: 0x00E46F90
 */
void ACL_$ENTER_SUPER(void);

/*
 * ACL_$EXIT_SUPER - Exit superuser mode
 *
 * Decrements the superuser mode counter.
 * Crashes if called without matching ENTER_SUPER.
 *
 * Original address: 0x00E46FB4
 */
void ACL_$EXIT_SUPER(void);

/*
 * ACL_$CLEAR_SUPER - Clear superuser mode
 *
 * Clears the superuser mode counter to zero and releases
 * any held locksmith override.
 *
 * Original address: 0x00E46FF8
 */
void ACL_$CLEAR_SUPER(void);

/*
 * ACL_$ENTER_SUBS - Enter subsystem context
 *
 * Enters a subsystem context with the specified UID's privileges.
 *
 * Parameters:
 *   uid        - UID of subsystem to enter
 *   status_ret - Output status code
 *
 * Returns:
 *   Non-zero if successful, 0 otherwise
 *
 * Original address: 0x00E46DA0
 */
int8_t ACL_$ENTER_SUBS(uid_t *uid, status_$t *status_ret);

/*
 * ============================================================================
 * Rights Checking
 * ============================================================================
 */

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
 * ACL_$RIGHTS_CHECK - Check access rights for an object (variant)
 *
 * Parameters:
 *   check_uid     - UID to check against (passed by value)
 *   file_uid      - UID of object to check
 *   required_mask - Pointer to required access rights mask (or NULL)
 *   option_flags  - Pointer to option flags (or NULL)
 *   check_flag    - Pointer to check flag
 *   status        - Output status code
 *
 * Returns:
 *   Non-zero if access granted, 0 if denied
 *
 * Original address: 0x00E46AEC
 */
int16_t ACL_$RIGHTS_CHECK(uid_t check_uid, uid_t *file_uid,
                          void *required_mask, void *option_flags,
                          int8_t *check_flag, status_$t *status);

/*
 * ACL_$CHECK_RIGHTS - Check rights with full options
 *
 * Original address: 0x00E46A8E
 */
int16_t ACL_$CHECK_RIGHTS(uid_t *uid, void *acl_data, void *options,
                          status_$t *status);

/*
 * ACL_$MIN_RIGHTS - Get minimum rights for an object
 *
 * Returns the minimum access rights that would be granted to any user
 * for the specified object (intersection of all ACL entries).
 *
 * Parameters:
 *   uid - UID of object to check
 *
 * Returns:
 *   Bitmask of rights (lower 4 bits valid)
 *
 * Original address: 0x00E468E2
 */
uint32_t ACL_$MIN_RIGHTS(uid_t *uid);

/*
 * ACL_$SET_ACL_CHECK - Check ACL permissions for set operation
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
 * ACL_$CHECK_FAULT_RIGHTS - Check fault handling rights between processes
 *
 * Determines if process pid1 can handle faults for process pid2.
 *
 * Parameters:
 *   pid1 - Pointer to fault handler process ID
 *   pid2 - Pointer to faulting process ID
 *
 * Returns:
 *   Non-zero if fault handling allowed, 0 otherwise
 *
 * Original address: 0x00E48A28
 */
int8_t ACL_$CHECK_FAULT_RIGHTS(int16_t *pid1, int16_t *pid2);

/*
 * ACL_$CHECK_DEBUG_RIGHTS - Check debug rights between processes
 *
 * Similar to CHECK_FAULT_RIGHTS but for debugging privileges.
 *
 * Parameters:
 *   pid1 - Pointer to debugger process ID
 *   pid2 - Pointer to debuggee process ID
 *
 * Returns:
 *   Non-zero if debug access allowed, 0 otherwise
 *
 * Original address: 0x00E48ADA
 */
int8_t ACL_$CHECK_DEBUG_RIGHTS(int16_t *pid1, int16_t *pid2);

/*
 * ACL_$USED_SUSER - Check if current process has used superuser privilege
 *
 * Returns non-zero if the current process has used superuser privilege
 * at any point (for auditing purposes).
 *
 * Returns:
 *   Non-zero if superuser was used, 0 otherwise
 *
 * Original address: 0x00E492CC
 */
int8_t ACL_$USED_SUSER(void);

/*
 * ============================================================================
 * SID Management
 * ============================================================================
 */

/*
 * ACL_$GET_EXSID - Get extended SID for current process
 *
 * Parameters:
 *   exsid  - Output buffer for extended SID
 *   status - Output status code
 *
 * Original address: 0x00E48972
 */
void ACL_$GET_EXSID(void *exsid, status_$t *status);

/*
 * ACL_$GET_PID_SID - Get SID for a specific process
 *
 * Parameters:
 *   pid        - Process ID
 *   sid_ret    - Output SID buffer
 *   status_ret - Output status code
 *
 * Original address: 0x00E489E2
 */
void ACL_$GET_PID_SID(int16_t pid, uid_t *sid_ret, status_$t *status_ret);

/*
 * ACL_$GET_RE_ALL_SIDS - Get all SIDs for current requester
 *
 * Parameters:
 *   acl_data  - Output buffer for ACL data (40 bytes)
 *   owner_uid - Output owner UID (8 bytes)
 *   prot_info - Output protection info (16 bytes)
 *   result    - Output result array (3 int32_t values)
 *   status    - Output status code
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
 * ============================================================================
 * ASID Management
 * ============================================================================
 */

/*
 * ACL_$ALLOC_ASID - Allocate an address space ID
 *
 * Parameters:
 *   asid_ret   - Output ASID
 *   status_ret - Output status code
 *
 * Original address: 0x00E73BB8
 */
void ACL_$ALLOC_ASID(int16_t *asid_ret, status_$t *status_ret);

/*
 * ACL_$FREE_ASID - Free an address space ID
 *
 * Resets the ACL state for an ASID to system defaults.
 *
 * Parameters:
 *   asid       - ASID to free
 *   status_ret - Output status code
 *
 * Original address: 0x00E74C6A
 */
void ACL_$FREE_ASID(int16_t asid, status_$t *status_ret);

/*
 * ACL_$GET_SID - Get SID for an ASID
 *
 * Parameters:
 *   asid    - Address space ID
 *   sid_ret - Output SID buffer
 *
 * Original address: 0x00E74C24
 */
void ACL_$GET_SID(int16_t asid, uid_t *sid_ret);

/*
 * ============================================================================
 * ACL Data Conversion
 * ============================================================================
 */

/*
 * ACL_$DEF_ACLDATA - Get default ACL data
 *
 * Parameters:
 *   acl_data_out - Output ACL data buffer (44 bytes)
 *   uid_out      - Output UID buffer (8 bytes, set to UID_$NIL)
 *
 * Original address: 0x00E478DC
 */
void ACL_$DEF_ACLDATA(void *acl_data_out, void *uid_out);

/*
 * ACL_$CONVERT_FUNKY_ACL - Convert "funky" ACL format
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
 * ============================================================================
 * Global Data
 * ============================================================================
 */

/* Default ACL UIDs for different object types */
extern uid_t ACL_$DNDCAL;   /* 0xE174DC: Default ACL for dirs/links */
extern uid_t ACL_$FNDWRX;   /* 0xE174C4: Default ACL for files */

#endif /* ACL_H */

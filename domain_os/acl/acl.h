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
 * Project List Management
 * ============================================================================
 */

/*
 * ACL_$ADD_PROJ - Add a project to the current process's project list
 *
 * Adds the specified project UID to the project list. Requires superuser.
 *
 * Parameters:
 *   proj_acl   - Project UID to add
 *   status_ret - Output status code
 *
 * Original address: 0x00E47EAC
 */
void ACL_$ADD_PROJ(uid_t *proj_acl, status_$t *status_ret);

/*
 * ACL_$DELETE_PROJ - Delete a project from the current process's project list
 *
 * Removes the specified project UID from the project list. Requires superuser.
 *
 * Parameters:
 *   proj_acl   - Project UID to remove
 *   status_ret - Output status code
 *
 * Original address: 0x00E47F54
 */
void ACL_$DELETE_PROJ(uid_t *proj_acl, status_$t *status_ret);

/*
 * ACL_$GET_PROJ_LIST - Get the project list for the current process
 *
 * Parameters:
 *   proj_acls  - Output buffer for project UIDs
 *   max_count  - Pointer to max count (clamped to 8)
 *   count_ret  - Output: actual count
 *   status_ret - Output status code
 *
 * Original address: 0x00E48034
 */
void ACL_$GET_PROJ_LIST(uid_t *proj_acls, int16_t *max_count, int16_t *count_ret,
                        status_$t *status_ret);

/*
 * ACL_$SET_PROJ_LIST - Set the project list for the current process
 *
 * Parameters:
 *   proj_acls  - Array of project UIDs to set
 *   count      - Pointer to count (max 8)
 *   status_ret - Output status code
 *
 * Original address: 0x00E480F4
 */
void ACL_$SET_PROJ_LIST(uid_t *proj_acls, int16_t *count, status_$t *status_ret);

/*
 * ============================================================================
 * SID Management (Extended)
 * ============================================================================
 */

/*
 * ACL_$GET_RE_SIDS - Get requestor SIDs for the current process
 *
 * Parameters:
 *   original_sids - Output buffer for original SIDs (36 bytes)
 *   current_sids  - Output buffer for current SIDs (36 bytes)
 *   status_ret    - Output status code
 *
 * Original address: 0x00E488B6
 */
void ACL_$GET_RE_SIDS(void *original_sids, void *current_sids, status_$t *status_ret);

/*
 * ACL_$GET_RES_SIDS - Get resource SIDs for the current process
 *
 * Parameters:
 *   original_sids - Output buffer for original SIDs (36 bytes)
 *   current_sids  - Output buffer for current SIDs (36 bytes)
 *   saved_sids    - Output buffer for saved SIDs (36 bytes)
 *   status_ret    - Output status code
 *
 * Original address: 0x00E4890C
 */
void ACL_$GET_RES_SIDS(void *original_sids, void *current_sids, void *saved_sids,
                       status_$t *status_ret);

/*
 * ACL_$GET_RES_ALL_SIDS - Get all resource SIDs and project lists
 *
 * Parameters:
 *   original_sids  - Output buffer for original SIDs (36 bytes)
 *   current_sids   - Output buffer for current SIDs (36 bytes)
 *   saved_sids     - Output buffer for saved SIDs (36 bytes)
 *   saved_proj     - Output buffer for saved project metadata (12 bytes)
 *   current_proj   - Output buffer for current project metadata (12 bytes)
 *   status_ret     - Output status code
 *
 * Original address: 0x00E4881C
 */
void ACL_$GET_RES_ALL_SIDS(void *original_sids, void *current_sids, void *saved_sids,
                           void *saved_proj, void *current_proj, status_$t *status_ret);

/*
 * ACL_$SET_RE_ALL_SIDS - Set all requestor SIDs for the current process
 *
 * Parameters:
 *   new_original_sids - New original SIDs (36 bytes)
 *   new_current_sids  - New current SIDs (36 bytes)
 *   new_saved_proj    - New saved project metadata (12 bytes)
 *   new_current_proj  - New current project metadata (12 bytes)
 *   status_ret        - Output status code
 *
 * Original address: 0x00E481AE
 */
void ACL_$SET_RE_ALL_SIDS(void *new_original_sids, void *new_current_sids,
                          void *new_saved_proj, void *new_current_proj,
                          status_$t *status_ret);

/*
 * ACL_$SET_RES_ALL_SIDS - Set all resource SIDs for the current process
 *
 * Parameters:
 *   new_original_sids - New original SIDs (36 bytes)
 *   new_current_sids  - New current SIDs (36 bytes)
 *   new_saved_sids    - New saved SIDs (36 bytes)
 *   new_saved_proj    - New saved project metadata (12 bytes)
 *   new_current_proj  - New current project metadata (12 bytes)
 *   status_ret        - Output status code
 *
 * Original address: 0x00E4855A
 */
void ACL_$SET_RES_ALL_SIDS(void *new_original_sids, void *new_current_sids,
                           void *new_saved_sids, void *new_saved_proj,
                           void *new_current_proj, status_$t *status_ret);

/*
 * ============================================================================
 * ACL Creation and Conversion
 * ============================================================================
 */

/*
 * ACL_$IMAGE - Create an ACL image
 *
 * Parameters:
 *   source_uid    - Source UID
 *   buffer_len    - Pointer to buffer length
 *   unknown_flag  - Pointer to flag byte
 *   param_4       - Unknown parameter
 *   param_5       - Unknown parameter
 *   param_6       - Unknown parameter
 *   status_ret    - Output status code
 *
 * Original address: 0x00E47DF6
 */
void ACL_$IMAGE(void *source_uid, int16_t *buffer_len, int8_t *unknown_flag,
                void *param_4, void *param_5, void *param_6, status_$t *status_ret);

/*
 * ACL_$PRIM_CREATE - Create a primitive ACL object
 *
 * Parameters:
 *   acl_data      - ACL data buffer
 *   data_len      - Pointer to length
 *   dir_uid       - Directory UID
 *   type          - ACL type code
 *   file_uid_ret  - Output: created file UID
 *   status_ret    - Output status code
 *
 * Original address: 0x00E47968
 */
void ACL_$PRIM_CREATE(void *acl_data, int16_t *data_len, uid_t *dir_uid,
                      int16_t type, uid_t *file_uid_ret, status_$t *status_ret);

/*
 * ACL_$CONVERT_TO_9ACL - Convert ACL to 9-entry format
 *
 * Parameters:
 *   type          - ACL type code
 *   source_uid    - Source UID
 *   dir_uid       - Directory UID
 *   default_prot  - Default protection
 *   result_uid    - Output: converted ACL UID
 *   status_ret    - Output status code
 *
 * Original address: 0x00E48CE8
 */
void ACL_$CONVERT_TO_9ACL(int16_t type, uid_t *source_uid, uid_t *dir_uid,
                          void *default_prot, uid_t *result_uid, status_$t *status_ret);

/*
 * ACL_$CONVERT_FROM_9ACL - Convert from 9-entry ACL format to new format
 *
 * Inverse of ACL_$CONVERT_TO_9ACL. Converts old 9-entry ACL format
 * to the new protection format.
 *
 * Parameters:
 *   source_acl   - Source ACL UID in 9-entry format
 *   acl_type     - ACL type UID
 *   prot_buf_out - Output: protection data buffer (44 bytes)
 *   prot_uid_out - Output: protection UID
 *   status_ret   - Output status code
 *
 * TODO: Locate and analyze actual implementation in Ghidra
 */
void ACL_$CONVERT_FROM_9ACL(uid_t *source_acl, uid_t *acl_type,
                             void *prot_buf_out, uid_t *prot_uid_out,
                             status_$t *status_ret);

/*
 * ACL_$COPY - Copy ACL from source to destination
 *
 * Parameters:
 *   source_acl_uid - Source ACL UID
 *   dest_uid       - Destination UID
 *   source_type    - Source ACL type
 *   dest_type      - Destination ACL type
 *   status_ret     - Output status code
 *
 * Original address: 0x00E4930A
 */
void ACL_$COPY(uid_t *source_acl_uid, uid_t *dest_uid, uid_t *source_type,
               uid_t *dest_type, status_$t *status_ret);

/*
 * ACL_$CONVERT_TO_10ACL - Convert ACL to 10-entry format
 *
 * Converts an ACL from older format to 10-entry format.
 *
 * Parameters:
 *   source_acl   - Source ACL UID
 *   file_uid     - File UID for conversion context
 *   result_uid   - Output: converted ACL UID
 *   acl_data     - Output: ACL data buffer (44 bytes)
 *   status_ret   - Output status code
 *
 * Returns:
 *   Non-zero if conversion was performed
 */
int8_t ACL_$CONVERT_TO_10ACL(void *source_acl, void *file_uid, uid_t *result_uid,
                              void *acl_data, status_$t *status_ret);

/*
 * ACL_$GET_ACL_ATTRIBUTES - Get ACL attributes for a file
 *
 * Retrieves ACL attributes (format info, flags) for the specified file.
 *
 * Parameters:
 *   file_uid   - UID of file to query
 *   flags      - Query flags
 *   attrs_out  - Output: attribute buffer (12 bytes)
 *   status_ret - Output status code
 */
void ACL_$GET_ACL_ATTRIBUTES(void *file_uid, int16_t flags, void *attrs_out,
                              status_$t *status_ret);

/*
 * ACL_$OVERRIDE_LOCAL_LOCKSMITH - Override local locksmith mode
 *
 * Temporarily enables or disables locksmith privilege override for the
 * current process. Used by remote file server to perform privileged
 * operations on behalf of remote clients.
 *
 * Parameters:
 *   enable     - Non-zero to enable override, 0 to disable
 *   status_ret - Output status code
 */
void ACL_$OVERRIDE_LOCAL_LOCKSMITH(int16_t enable, status_$t *status_ret);

/*
 * ============================================================================
 * Subsystem and Locksmith
 * ============================================================================
 */

/*
 * ACL_$INHERIT_SUBSYS - Inherit subsystem state from parent
 *
 * Parameters:
 *   inherit_flag - Pointer to inheritance flag byte
 *   status_ret   - Output status code
 *
 * Original address: 0x00E49138
 */
void ACL_$INHERIT_SUBSYS(uint8_t *inherit_flag, status_$t *status_ret);

/*
 * ACL_$SET_LOCAL_LOCKSMITH - Set local locksmith mode
 *
 * Parameters:
 *   locksmith_value - Pointer to new locksmith value
 *   status_ret      - Output status code
 *
 * Original address: 0x00E49196
 */
void ACL_$SET_LOCAL_LOCKSMITH(int16_t *locksmith_value, status_$t *status_ret);

/*
 * ============================================================================
 * Global Data
 * ============================================================================
 */

/* Default ACL UIDs for different object types */
extern uid_t ACL_$DNDCAL;   /* 0xE174DC: Default ACL for dirs/links */
extern uid_t ACL_$FNDWRX;   /* 0xE174C4: Default ACL for files */
extern uid_t ACL_$DIR_ACL;  /* Well-known ACL UID for directories */

/* ACL type UIDs - used to identify ACL operations */
extern uid_t ACL_$FILE_ACL;    /* 0xE1744C */
extern uid_t ACL_$FILEIN_ACL;  /* 0xE17454 */

#endif /* ACL_H */

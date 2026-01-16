/*
 * FILE - File Operations Module (Internal Header)
 *
 * Internal types, constants, and helper functions used only within
 * the FILE subsystem.
 */

#ifndef FILE_INTERNAL_H
#define FILE_INTERNAL_H

#include "file/file.h"
#include "uid/uid.h"
#include "ast/ast.h"
#include "acl/acl.h"
#include "time/time.h"
#include "hint/hint.h"
#include "vtoce/vtoce.h"
#include "rem_file/rem_file.h"

/*
 * ============================================================================
 * Memory Addresses (m68k)
 * ============================================================================
 */

#ifdef M68K_TARGET
/* Lock control block base address */
#define FILE_LOCK_CONTROL_ADDR      0xE82128

/* Lock table base address (58 entries × 300 bytes) */
#define FILE_LOCK_TABLE_ADDR        0xE9F9CC

/* Lock entries base address (1792 entries × 28 bytes) */
#define FILE_LOCK_ENTRIES_ADDR      0xE935CC

/* UID lock eventcount address */
#define FILE_UID_LOCK_EC_ADDR       0xE2C028

/* Secondary table address (58 words) */
#define FILE_LOCK_TABLE2_ADDR       0xEA3DC4
#endif

/*
 * ============================================================================
 * Internal Data Structures
 * ============================================================================
 */

/* Secondary lock table (58 words, purpose TBD) */
extern uint16_t FILE_$LOCK_TABLE2[];

/*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/*
 * OS_PROC_SHUTWIRED - Shutdown wired pages (called on access denial)
 *
 * Called when access rights check fails. Purpose is to release
 * any wired pages and resources.
 *
 * Parameters:
 *   status - Output status code
 *
 * Original address: 0x00E5D050
 */
extern void OS_PROC_SHUTWIRED(status_$t *status);

/*
 * FILE_$DELETE_INT - Internal delete handler
 *
 * Declared in file.h, used internally for refcount operations.
 */

/*
 * ============================================================================
 * Internal Protection/ACL Functions
 * ============================================================================
 */

/*
 * FILE_$SET_PROT_INT - Set file protection (internal)
 *
 * Core internal function for setting file protection. Handles both local
 * and remote files, ACL checking, and locksmith privileges.
 *
 * Parameters:
 *   file_uid    - UID of file to modify
 *   acl_data    - ACL data buffer (44 bytes)
 *   attr_type   - Protection attribute type
 *   prot_type   - Protection type being set
 *   subsys_flag - Subsystem data flag (negative to allow override)
 *   status_ret  - Output status code
 *
 * Original address: 0x00E5DD08
 */
void FILE_$SET_PROT_INT(uid_t *file_uid, void *acl_data, uint16_t attr_type,
                        uint16_t prot_type, int16_t subsys_flag,
                        status_$t *status_ret);

/*
 * FILE_$CHECK_SAME_VOLUME - Check if two files are on the same volume
 *
 * Checks if two file UIDs refer to objects on the same volume.
 * Used during protection operations to verify source/target locations.
 *
 * Parameters:
 *   file_uid1     - First file UID
 *   file_uid2     - Second file UID
 *   copy_location - If negative, copy location info on remote hit
 *   location_out  - Output buffer for location info (32 bytes)
 *   status_ret    - Output status code
 *
 * Returns:
 *   0: Files are on different volumes or error occurred
 *   -1: Files are local and on same logical volume
 *
 * Original address: 0x00E5E476
 */
int8_t FILE_$CHECK_SAME_VOLUME(uid_t *file_uid1, uid_t *file_uid2,
                                int8_t copy_location, uint32_t *location_out,
                                status_$t *status_ret);

/*
 * FILE_$AUDIT_SET_PROT - Log audit event for protection changes
 *
 * Logs a protection/ACL change audit event if auditing is enabled.
 *
 * Parameters:
 *   file_uid   - UID of file being modified
 *   acl_data   - ACL data buffer (44 bytes)
 *   prot_info  - Protection info (8 bytes)
 *   prot_type  - Protection type being set
 *   status     - Status of the operation
 *
 * Original address: 0x00E5DC96
 */
void FILE_$AUDIT_SET_PROT(uid_t *file_uid, void *acl_data, void *prot_info,
                          uint16_t prot_type, status_$t status);

/*
 * ============================================================================
 * External ACL Functions
 * ============================================================================
 */

/*
 * ACL_$SET_ACL_CHECK - Check ACL permissions for set operation
 *
 * Verifies the caller has permission to modify ACL.
 *
 * Original address: 0x00E470C4
 */
int8_t ACL_$SET_ACL_CHECK(uid_t *file_uid, void *acl_data, uid_t *source_uid,
                          int16_t *prot_type, int8_t *permission_flags,
                          status_$t *status_ret);

/*
 * ACL_$GET_LOCAL_LOCKSMITH - Check if local locksmith mode is enabled
 *
 * Returns:
 *   0 if locksmith mode is enabled (has privileges)
 *   Non-zero otherwise
 *
 * Original address: 0x00E4923C
 */
int16_t ACL_$GET_LOCAL_LOCKSMITH(void);

/*
 * ACL_$CONVERT_FUNKY_ACL - Convert "funky" ACL format
 *
 * Converts an ACL UID in the "funky" format to standard ACL components.
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
 * Original address: 0x00E478DC
 */
void ACL_$DEF_ACLDATA(void *acl_data_out, void *uid_out);

/*
 * ============================================================================
 * External AST Functions
 * ============================================================================
 */

/*
 * AST_$GET_DISM_SEQN - Get dismount sequence number
 *
 * Returns a sequence number that changes when volumes are dismounted.
 * Used to detect if volume state changed during an operation.
 *
 * Original address: 0x00E01388
 */
int32_t AST_$GET_DISM_SEQN(void);

/*
 * AST_$GET_COMMON_ATTRIBUTES - Get common file attributes
 *
 * Original address: 0x00E04A00
 */
void AST_$GET_COMMON_ATTRIBUTES(void *uid_buf, uint16_t mode,
                                 void *attr_buf, status_$t *status_ret);

/*
 * ============================================================================
 * External REM_FILE Functions
 * ============================================================================
 */

/*
 * REM_FILE_$NEIGHBORS - Get neighbor info for remote file
 *
 * Original address: 0x00E621B8
 */
int8_t REM_FILE_$NEIGHBORS(void *location_info, void *uid1, void *uid2,
                            status_$t *status_ret);

/*
 * REM_FILE_$FILE_SET_PROT - Set protection on remote file
 *
 * Original address: 0x00E62B64
 */
void REM_FILE_$FILE_SET_PROT(void *location_info, uid_t *file_uid,
                              void *acl_data, uint16_t attr_type,
                              void *exsid, uint8_t subsys_flag,
                              void *attr_result, status_$t *status_ret);

/*
 * ============================================================================
 * External AUDIT Functions
 * ============================================================================
 */

/*
 * AUDIT_$LOG_EVENT - Log an audit event
 *
 * Original address: 0x00E70DF6
 */
void AUDIT_$LOG_EVENT(void *event_id, void *flags, void *status,
                       void *data, void *info);

#endif /* FILE_INTERNAL_H */

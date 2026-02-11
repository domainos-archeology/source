/*
 * name_$old_add_link - Add link with remote/local handling
 *
 * Shared implementation for DIR_$OLD_ADDU (hard_link_flag=0) and
 * DIR_$OLD_ADD_HARD_LINKU (hard_link_flag=0xFF). Determines whether
 * the target object is local or remote, and dispatches accordingly.
 *
 * Process:
 * 1. Copy target UID (file_uid) into local variables
 * 2. Call AST_$GET_LOCATION to determine target's location
 *    - On failure (unless file_$object_not_found with flags): return error
 * 3. Call ACL_$RIGHTS to check directory ACL permissions
 * 4. Copy directory UID, call AST_$GET_LOCATION for directory
 * 5. If both objects are remote (both have bit 7 set):
 *    a. Validate leaf name via name_$validate_leaf
 *    b. Call REM_FILE_$NAME_ADD_HARD_LINKU for remote link creation
 *    c. On comms failure: retry via FILE_$READ_LOCK_ENTRYUI
 *    d. Convert file_$bad_reply to file_$op_cannot_perform_here
 * 6. If local:
 *    a. Set attribute value to 1
 *    b. Conditionally call AST_$SET_ATTRIBUTE(type 6) to increment link count
 *    c. Call FUN_00e565b8 for local link addition
 *    d. On failure: call AST_$SET_ATTRIBUTE(type 7) to rollback
 *
 * Parameters:
 *   dir_uid        - UID of parent directory
 *   name           - Name for the new entry
 *   name_len       - Length of name
 *   file_uid       - UID of target file
 *   hard_link_flag - 0 for normal add, 0xFF for hard link
 *   status_ret     - Output: status code
 *
 * Original address: 0x00E5674C
 * Size: 506 bytes
 *
 * TODO: Full implementation requires understanding AST_$GET_LOCATION,
 * REM_FILE_$NAME_ADD_HARD_LINKU remote protocol, FILE_$READ_LOCK_ENTRYUI
 * retry logic, and FUN_00e565b8 local link addition.
 */

#include "dir/dir_internal.h"

/* Stub - 506-byte add link with remote/local dispatch */

/*
 * dir_$do_op_add_link - DO_OP handler for add entry/hard link
 *
 * Server-side handler for remote add (op 0x2A) and add hard link (op 0x2C)
 * operations dispatched by DIR_$DO_OP.
 *
 * Process:
 * 1. Call FUN_00e4fef2 with FUN_00e4c9e4 as callback for name resolution
 * 2. Copy target UID, clear bit 6 of local flags byte
 * 3. Call AST_$GET_COMMON_ATTRIBUTES (type 0x90) for the target object
 * 4. If object not found and flags (param_5) >= 0: set bit 7, continue
 * 5. If link count is zero: skip rights check, go to attribute set
 * 6. Check ACL rights via ACL_$RIGHTS:
 *    - status_$insufficient_rights_to_perform_operation
 *    - status_$no_right_to_perform_operation
 *    - status_$naming_insufficient_rights (if rights mask & 0x48 == 0x40)
 * 7. If link count >= 0xFFF5: return status_$naming_too_many_hard_links
 * 8. Call AST_$SET_ATTRIBUTE(type 6, value 1) to increment link count
 * 9. On failure: call FUN_00e511da to undo
 *
 * Parameters:
 *   uid        - UID of the directory
 *   name       - Entry name
 *   name_len   - Length of name
 *   file_uid   - UID of file to link
 *   flags      - 0 for add, 0xFF for add hard link
 *   status_ret - Output: status code
 *
 * Original address: 0x00E5044A
 * Size: 378 bytes
 *
 * TODO: Full implementation requires understanding FUN_00e4fef2 (name
 * resolution helper) and FUN_00e511da (undo/cleanup helper).
 */

#include "dir/dir_internal.h"

/* Stub - 378-byte DO_OP add link handler */

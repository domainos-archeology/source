/*
 * name_$old_get_entry_nonroot - Non-root directory entry lookup
 *
 * Resolves a directory entry by name for non-root directories.
 * Process:
 * 1. Validate the leaf name via name_$validate_leaf
 * 2. Get location hints for the directory via HINT_$GET_HINTS
 * 3. For each hint: try remote lookup via REM_FILE_$NAME_GET_ENTRYU
 *    - If remote returns status_$naming_name_not_found, fall through
 *      to local lookup
 *    - On success with a remote UID, update hint table
 * 4. If local (NODE_$ME matches) or remote failed:
 *    - Lock directory via NAME_$LOCK_DIR
 *    - Find entry via dir_$old_find_entry
 *    - Extract entry type, UID, and location info
 *    - If entry is on a different node, update hints
 *    - Unlock directory
 * 5. If no hints found, CRASH_SYSTEM
 *
 * Parameters:
 *   dir_uid    - UID of directory to search
 *   name       - Name to look up
 *   name_len   - Length of name
 *   entry_ret  - Output: entry information buffer
 *   status_ret - Output: status code
 *
 * Original address: 0x00E57CE0
 * Size: 656 bytes
 *
 * TODO: Full implementation requires careful handling of hint table,
 * remote/local fallback logic, and UID location comparison.
 * The assembly has been verified against Ghidra output.
 */

#include "dir/dir_internal.h"

/* Stub - complex 656-byte function with remote/local/hint fallback */

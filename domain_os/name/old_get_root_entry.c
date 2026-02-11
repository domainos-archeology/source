/*
 * name_$old_get_root_entry - Root directory entry lookup
 *
 * Resolves a name in the root directory. First tries local lookup
 * via name_$old_get_entry_nonroot. If that fails with
 * status_$naming_name_not_found AND the directory UID matches
 * NAME_$ROOT_UID, queries remote nodes via REM_NAME_$GET_ENTRY.
 *
 * On successful remote lookup:
 * 1. If entry type == 1:
 *    a. Copy entry type to result[0]
 *    b. Copy 12 bytes of entry data to result[2..]
 *    c. Unmap case on the name
 *    d. Extract UID from result for caching
 *    e. Cache entry via name_$old_add_entry
 * 2. If entry type != 1:
 *    Return status_$naming_name_not_found
 *
 * Parameters:
 *   dir_uid    - UID of directory (expected to be root)
 *   name       - Name to look up
 *   name_len   - Length of name
 *   entry_ret  - Output: entry information
 *   status_ret - Output: status code
 *
 * Returns: entry type (short)
 *
 * Original address: 0x00E57F74
 * Size: 226 bytes
 *
 * TODO: Implement fully - requires REM_NAME_$GET_ENTRY integration
 * and understanding of the entry result buffer format.
 */

#include "dir/dir_internal.h"

/* Stub - 226-byte root directory entry lookup with remote fallback */

/*
 * name_$old_drop_entry - Name-level drop directory entry
 *
 * Validates the leaf name, locks the directory, removes the entry via
 * an internal helper, and unlocks. Used by DIR_$OLD_DROP_DIRU,
 * NAME_$OLD_DELETE_ENTRYU, and DIR_$OLD_VALIDATE_ROOT_ENTRY.
 *
 * Process:
 * 1. Validate leaf name via name_$validate_leaf
 *    - On failure: return status_$naming_invalid_leaf
 * 2. Lock directory via NAME_$LOCK_DIR (with flags from type param)
 * 3. Call dir_$old_unlink_entry to perform entry removal
 * 4. Unlock directory via NAME_$UNLOCK_DIR
 *    - Propagate unlock errors if no prior error
 * 5. Exit super mode via ACL_$EXIT_SUPER
 *
 * Parameters:
 *   dir_uid    - UID of directory
 *   name       - Entry name to remove
 *   name_len   - Length of name
 *   entry_data - Additional entry data / flags
 *
 * TODO: Ghidra decompilation shows 6 parameters (including param_4 as
 * type/flags, param_5 as extra data, and status_ret). Existing callers
 * pass only 4 arguments. Verify parameter count against assembly at
 * each call site.
 *
 * Original address: 0x00E56A04
 * Size: 150 bytes
 */

#include "dir/dir_internal.h"

/* Stub - 150-byte name-level drop entry */

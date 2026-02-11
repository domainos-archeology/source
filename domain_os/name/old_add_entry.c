/*
 * name_$old_add_entry - Name-level add directory entry
 *
 * Validates the leaf name, locks the directory, adds the entry via
 * an internal helper, updates the hint table, and unlocks.
 *
 * Process:
 * 1. Validate leaf name via name_$validate_leaf
 *    - On failure: return status_$naming_invalid_leaf
 * 2. Lock directory via NAME_$LOCK_DIR (with flags from type param)
 * 3. Call FUN_00e55406 to add entry to directory buffer
 *    (with UID, type, location data, replace flag 0xFF)
 * 4. On success:
 *    - Extract location info from target UID
 *    - Update hint table via HINT_$ADDI
 * 5. Unlock directory via NAME_$UNLOCK_DIR
 *    - Propagate unlock errors
 * 6. Exit super mode via ACL_$EXIT_SUPER
 *
 * Parameters:
 *   dir_uid    - UID of directory
 *   type       - Entry type code (e.g., 2 for root entries)
 *   name       - Entry name
 *   name_len   - Length of name
 *   file_uid   - UID of file to add
 *   flags      - Operation flags (location info)
 *   status_ret - Output: status code
 *
 * Original address: 0x00E56682
 * Size: 202 bytes
 */

#include "dir/dir_internal.h"

void name_$old_add_entry(uid_t *dir_uid, uint16_t type, char *name,
                         uint16_t name_len, uid_t *file_uid,
                         uint32_t flags, status_$t *status_ret)
{
    int8_t valid;
    uint16_t parsed_len;
    uint32_t handle;
    status_$t unlock_status;
    uint8_t parsed_name[32];
    uint32_t location;
    uint32_t loc_low;
    uint8_t result[4];

    valid = name_$validate_leaf(name, name_len, parsed_name, &parsed_len);
    if (valid < 0) {
        /* Valid leaf name */
        NAME_$LOCK_DIR(dir_uid, &handle, ((uint32_t)4 << 16) | type, status_ret);
        if (*status_ret == status_$ok) {
            FUN_00e55406(dir_uid, handle, parsed_name, parsed_len,
                         1, file_uid, flags, 0xFF, result, status_ret);
            if (*status_ret == status_$ok) {
                location = flags;
                loc_low = file_uid->low & 0xFFFFF;
                HINT_$ADDI(file_uid, &location);
            }
            NAME_$UNLOCK_DIR(&unlock_status);
            if (unlock_status != status_$ok) {
                *status_ret = unlock_status;
            }
        }
        ACL_$EXIT_SUPER();
    } else {
        *status_ret = status_$naming_invalid_leaf;
    }
}

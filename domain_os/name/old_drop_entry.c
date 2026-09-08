/*
 * name_$old_drop_entry - Name-level drop directory entry
 *
 * Validates the leaf name, locks the directory, removes the entry via
 * dir_$old_unlink_entry, and unlocks. Used by DIR_$OLD_DROP_DIRU,
 * NAME_$OLD_DELETE_ENTRYU, and DIR_$OLD_VALIDATE_ROOT_ENTRY.
 *
 * Process:
 * 1. Validate leaf name via name_$validate_leaf
 *    - On failure: return status_$naming_invalid_leaf
 * 2. Lock directory via NAME_$LOCK_DIR (ACL flags=4, mode=type)
 * 3. Call dir_$old_unlink_entry with op_type=1 to perform entry removal
 * 4. Unlock directory via NAME_$UNLOCK_DIR
 *    - Propagate unlock status only if unlink had no error
 * 5. Exit super mode via ACL_$EXIT_SUPER
 *
 * Parameters:
 *   dir_uid    - UID of directory containing the entry
 *   name       - Entry name to remove
 *   name_len   - Length of name
 *   type       - Lock mode flags (low word of NAME_$LOCK_DIR flags)
 *   result     - Output buffer for unlinked entry UID
 *   status_ret - Output: status code
 *
 * Original address: 0x00E56A04
 * Size: 150 bytes
 *
 * Assembly verification:
 *   link.w A6,-0x34          ; 52-byte frame
 *   A6+0x08: dir_uid (uid_t*)
 *   A6+0x0C: name (char*)
 *   A6+0x10: name_len (uint16_t)
 *   A6+0x12: type (uint16_t)
 *   A6+0x14: result (void*)
 *   A6+0x18: status_ret (status_$t*)
 *   Locals: parsed_name[32] at -0x28, unlock_status at -0x2C,
 *           handle at -0x30, parsed_len at -0x32
 */

#include "name/name_internal.h"
#include "dir/dir.h"

void name_$old_drop_entry(uid_t *dir_uid, char *name, uint16_t name_len,
                          uint16_t type, void *result, status_$t *status_ret)
{
    int8_t valid;
    uint16_t parsed_len;
    uint32_t handle;
    status_$t unlock_status;
    uint8_t parsed_name[32];

    valid = name_$validate_leaf(name, name_len, parsed_name, &parsed_len);
    if (valid < 0) {
        /* Valid leaf name - lock directory and remove entry */
        NAME_$LOCK_DIR(dir_uid, &handle, 4, (int16_t)type, status_ret);
        if (*status_ret == status_$ok) {
            dir_$old_unlink_entry(dir_uid, handle, parsed_name, parsed_len,
                                  1, result, status_ret);
            /*
             * 0x00E56A76-0x00E56A86: the unlock reports into its own cell and
             * replaces the caller's status only when the caller's LOW WORD is
             * still zero (`tst.w (0x2,A3)` / `bne`).
             */
            NAME_$UNLOCK_DIR(&unlock_status);
            if ((int16_t)*status_ret == 0) {
                *status_ret = unlock_status;
            }
        }
        ACL_$EXIT_SUPER();
    } else {
        *status_ret = status_$naming_invalid_leaf;
    }
}

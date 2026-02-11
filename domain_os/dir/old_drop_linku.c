/*
 * DIR_$OLD_DROP_LINKU - Legacy drop soft link
 *
 * Validates the leaf name, acquires directory lock, performs
 * the drop link operation, then releases the lock.
 *
 * Original address: 0x00E57924
 * Original size: 156 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_DROP_LINKU - Legacy drop soft link
 *
 * Validates the leaf name via name_$validate_leaf. If valid,
 * acquires the directory lock via NAME_$LOCK_DIR with flags=0x40002,
 * then calls dir_$old_unlink_entry with op_type=3 to drop the link entry.
 * Finally releases the lock via NAME_$UNLOCK_DIR and exits super mode.
 *
 * Parameters:
 *   dir_uid    - UID of parent directory
 *   name       - Name of link to drop
 *   name_len   - Pointer to name length
 *   target_uid - Output: UID associated with link (unused for soft links)
 *   status_ret - Output: status code
 */
void DIR_$OLD_DROP_LINKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                         uid_t *target_uid, status_$t *status_ret)
{
    uint8_t parsed_name[256];
    uint16_t parsed_len;
    uint32_t handle;
    int8_t valid;

    /* Validate and parse the leaf name */
    valid = name_$validate_leaf(name, *name_len, parsed_name, &parsed_len);
    if (valid >= 0) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* Acquire directory lock */
    NAME_$LOCK_DIR(dir_uid, &handle, 0x40002, status_ret);
    if ((int16_t)*status_ret != 0) {
        ACL_$EXIT_SUPER();
        return;
    }

    /* Perform the drop link operation (op_type=3) */
    dir_$old_unlink_entry(dir_uid, handle, parsed_name, parsed_len,
                 3, NULL, status_ret);

    /* Release directory lock */
    NAME_$UNLOCK_DIR(status_ret);

    ACL_$EXIT_SUPER();
}

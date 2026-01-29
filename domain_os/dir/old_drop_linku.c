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
 * Validates the leaf name via FUN_00e54414. If valid,
 * acquires the directory lock via FUN_00e54854 with flags=0x40002,
 * then calls FUN_00e5569c with op_type=3 to drop the link entry.
 * Finally releases the lock via FUN_00e54734 and exits super mode.
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
    valid = FUN_00e54414(name, *name_len, parsed_name, &parsed_len);
    if (valid >= 0) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* Acquire directory lock */
    FUN_00e54854(dir_uid, &handle, 0x40002, status_ret);
    if ((int16_t)*status_ret != 0) {
        ACL_$EXIT_SUPER();
        return;
    }

    /* Perform the drop link operation (op_type=3) */
    FUN_00e5569c(dir_uid, handle, parsed_name, parsed_len,
                 3, NULL, status_ret);

    /* Release directory lock */
    FUN_00e54734(status_ret);

    ACL_$EXIT_SUPER();
}

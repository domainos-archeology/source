/*
 * DIR_$OLD_ADD_LINKU - Legacy add soft/symbolic link
 *
 * Validates the leaf name, maps the case, validates the path,
 * enters super mode, and adds the link entry.
 *
 * Original address: 0x00E576EA
 * Original size: 264 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_ADD_LINKU - Legacy add soft/symbolic link
 *
 * Creates a symbolic link entry in a directory. The process is:
 * 1. Validate the leaf name via FUN_00e54414
 * 2. Map the name to uppercase via MAP_CASE
 * 3. Validate the path via NAME_$VALIDATE
 * 4. Check if this is the root directory (reject if so)
 * 5. Enter super mode via FUN_00e54854
 * 6. Add the link via FUN_00e5545c
 * 7. Release lock via FUN_00e54734
 * 8. Exit super mode via ACL_$EXIT_SUPER
 *
 * Parameters:
 *   dir_uid    - UID of directory for link
 *   name       - Name for the link
 *   name_len   - Pointer to name length
 *   target     - Target pathname
 *   target_len - Pointer to target length
 *   status_ret - Output: status code
 */
void DIR_$OLD_ADD_LINKU(uid_t *dir_uid, char *name, int16_t *name_len,
                        void *target, uint16_t *target_len, status_$t *status_ret)
{
    uint8_t parsed_name[32];
    uint16_t parsed_len;
    uint32_t handle;
    int8_t valid;
    int8_t truncated;
    int16_t consumed;
    start_path_type_t path_type;
    uint8_t result_buf[4];
    status_$t local_status;
    char mapped_target[256];
    int16_t max_out_len;
    uint16_t mapped_len;

    /* Validate and parse the leaf name */
    valid = FUN_00e54414(name, (uint16_t)*name_len, parsed_name, &parsed_len);
    if (valid >= 0) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* Map target to uppercase for case-insensitive comparison */
    max_out_len = 0x100;  /* 256 - max output buffer */
    MAP_CASE((char *)target, (int16_t *)target_len, mapped_target,
             &max_out_len, (int16_t *)&mapped_len, (uint8_t *)&truncated);
    if (truncated < 0) {
        *status_ret = status_$naming_invalid_link;
        return;
    }

    /* Validate the target pathname */
    valid = NAME_$VALIDATE(mapped_target, &mapped_len,
                           &consumed, &path_type);
    if (valid >= 0) {
        *status_ret = status_$naming_invalid_link;
        return;
    }

    /* Root directory cannot have symbolic links */
    if (dir_uid->high == NAME_$ROOT_UID.high &&
        dir_uid->low == NAME_$ROOT_UID.low) {
        *status_ret = status_$naming_invalid_link_operation;
        return;
    }

    /* Enter super mode / acquire directory lock */
    FUN_00e54854(dir_uid, &handle, 0x40002, status_ret);
    if (*status_ret != status_$ok) {
        ACL_$EXIT_SUPER();
        return;
    }

    /* Add the link entry */
    FUN_00e5545c(dir_uid, handle, parsed_name, parsed_len,
                 mapped_target, mapped_len, 0, result_buf, status_ret);

    /* Release directory lock */
    FUN_00e54734(&local_status);
    if (local_status != status_$ok) {
        *status_ret = local_status;
    }

    ACL_$EXIT_SUPER();
}

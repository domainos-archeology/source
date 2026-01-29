/*
 * DIR_$OLD_CREATE_DIRU - Legacy create subdirectory
 *
 * Creates a new directory as a child of the given directory.
 * Validates the leaf, enters super mode, creates the directory
 * object, adds the entry, and handles cleanup on failure.
 *
 * Original address: 0x00E571AE
 * Original size: 414 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_CREATE_DIRU - Legacy create subdirectory
 *
 * The process is:
 * 1. Validate the leaf name via FUN_00e54414
 * 2. Enter super mode / acquire directory lock via FUN_00e54854
 * 3. Save per-process state
 * 4. Create directory object via FUN_00e54546
 * 5. Add entry via FUN_00e55220
 * 6. On failure, clean up with SET_DEFAULT_ACL + TRUNCATE
 * 7. Restore state, release lock, exit super mode
 *
 * Parameters:
 *   parent_uid  - UID of parent directory
 *   name        - Name for new directory
 *   name_len    - Pointer to name length
 *   new_dir_uid - Output: UID of created directory
 *   status_ret  - Output: status code
 */
void DIR_$OLD_CREATE_DIRU(uid_t *parent_uid, char *name, uint16_t *name_len,
                          uid_t *new_dir_uid, status_$t *status_ret)
{
    uint8_t parsed_name[256];
    uint16_t parsed_len;
    uint32_t handle;
    int8_t valid;
    uid_t created_uid;
    uint8_t result_buf[8];
    status_$t cleanup_status;

    /* Validate and parse the leaf name */
    valid = FUN_00e54414(name, (uint16_t)*name_len, parsed_name, &parsed_len);
    if (valid >= 0) {
        *status_ret = status_$naming_invalid_leaf;
        return;
    }

    /* Enter super mode / acquire directory lock */
    FUN_00e54854(parent_uid, &handle, 0x40002, status_ret);
    if ((int16_t)*status_ret != 0) {
        ACL_$EXIT_SUPER();
        return;
    }

    /* Create the directory object */
    FUN_00e54546(parent_uid, handle, 2, &created_uid, status_ret);
    if ((int16_t)*status_ret != 0) {
        FUN_00e54734(&cleanup_status);
        ACL_$EXIT_SUPER();
        return;
    }

    /* Add the directory entry */
    FUN_00e55220(parent_uid, handle, parsed_name, parsed_len,
                 2, &created_uid, 0, result_buf, status_ret);
    if ((int16_t)*status_ret != 0) {
        /* Clean up on failure - set default ACLs and truncate */
        DIR_$OLD_SET_DEFAULT_ACL(&created_uid, &ACL_$DIR_ACL,
                                 &ACL_$NIL, &cleanup_status);
        if ((int16_t)cleanup_status == 0) {
            DIR_$OLD_SET_DEFAULT_ACL(&created_uid, &ACL_$FILE_ACL,
                                     &ACL_$NIL, &cleanup_status);
        }
        /* TODO: Additional cleanup - truncate and delete the created dir */
        FUN_00e54734(&cleanup_status);
        ACL_$EXIT_SUPER();
        return;
    }

    /* Return the created directory UID */
    new_dir_uid->high = created_uid.high;
    new_dir_uid->low = created_uid.low;

    /* Release directory lock */
    FUN_00e54734(status_ret);

    ACL_$EXIT_SUPER();
}

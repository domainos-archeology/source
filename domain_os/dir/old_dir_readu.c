/*
 * DIR_$OLD_DIR_READU - Legacy directory read
 *
 * Validates that the directory is not the canned replicated root
 * (which would be a protocol error), then delegates to FUN_00e579c0
 * for the actual read operation.
 *
 * Original address: 0x00E57C80
 * Original size: 92 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_DIR_READU - Legacy directory read
 *
 * If the target directory is the canned replicated root, crashes
 * the system (this should never happen in the OLD protocol path).
 * Otherwise, dereferences param_3 and param_4 and passes all
 * parameters to FUN_00e579c0.
 *
 * Assembly analysis:
 *   param_1 (0x08,A6): uid_t* uid
 *   param_2 (0x0C,A6): void*  (passed directly)
 *   param_3 (0x10,A6): void*  (dereferenced: *(uint32_t*)param_3)
 *   param_4 (0x14,A6): void*  (dereferenced: *(uint32_t*)param_4)
 *   param_5 (0x18,A6): void*  (passed directly)
 *   param_6 (0x1C,A6): void*  (passed directly)
 *   param_7 (0x20,A6): status_$t* status_ret
 *
 * Parameters:
 *   uid        - UID of directory to read
 *   param_2    - Read context/buffer
 *   param_3    - Pointer to max_entries (dereferenced)
 *   param_4    - Pointer to count (dereferenced)
 *   param_5    - Flags/options
 *   param_6    - EOF indicator
 *   status_ret - Output: status code
 */
void DIR_$OLD_DIR_READU(uid_t *uid, void *param_2, void *param_3,
                        void *param_4, void *param_5, void *param_6,
                        status_$t *status_ret)
{
    /* Check for canned replicated root - should not use OLD protocol */
    if (uid->high == NAME_$CANNED_REP_ROOT_UID.high &&
        uid->low == NAME_$CANNED_REP_ROOT_UID.low) {
        CRASH_SYSTEM((const status_$t *)&Bad_request_header_version_err);
    }

    /* Delegate to internal read implementation
     * Note: param_3 and param_4 are dereferenced before passing */
    FUN_00e579c0(uid, param_2, *(uint32_t *)param_3, *(uint32_t *)param_4,
                 param_5, param_6, status_ret);
}

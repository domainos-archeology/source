/*
 * dir_$do_op_drop_entry - DO_OP handler for drop/remove entry
 *
 * Server-side handler for drop operations dispatched by DIR_$DO_OP.
 * Opens the directory for write access, removes the named entry via
 * dir_$remove_entry, and provides idempotent handling for server
 * processes (type 9): if the entry is not found and the current
 * process is a server process, the error is silently cleared.
 *
 * Parameters:
 *   uid        - Directory UID
 *   rights     - ACL rights required for the operation
 *   name       - Entry name to drop
 *   name_len   - Length of name
 *   op_type    - Entry type for validation (passed to dir_$remove_entry)
 *   result_uid - Output: UID of removed entry
 *   status_ret - Output: status code
 *
 * Original address: 0x00E511DA
 * Original size: 132 bytes
 */

#include "dir/dir_internal.h"

void dir_$do_op_drop_entry(uid_t *uid, uint16_t rights, void *name,
                           uint16_t name_len, uint16_t op_type,
                           void *result_uid, status_$t *status_ret)
{
    uint32_t local_handle;

    ACL_$ENTER_SUPER();

    /* Open directory for write access with specified rights */
    dir_$open_dir(uid, 2, rights, &local_handle, status_ret);

    if (*status_ret == status_$ok) {
        /* Remove the named entry */
        dir_$remove_entry((void *)(uintptr_t)local_handle, name, name_len,
                          op_type, result_uid, status_ret);
    }

    /* Idempotent handling for server processes (type 9):
     * If entry not found and we're a server process, clear the error.
     * This handles retried operations where the entry was already removed. */
    if (*status_ret == status_$naming_name_not_found) {
        if (*(int16_t *)((char *)PROC1_$TYPE +
            (int16_t)(PROC1_$CURRENT * 2)) == 9) {
            *status_ret = status_$ok;
        }
    }

    dir_$release_handle(&local_handle);
    ACL_$EXIT_SUPER();
}

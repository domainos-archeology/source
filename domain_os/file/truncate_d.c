/*
 * FILE_$TRUNCATE_D - Truncate a file with domain context
 *
 * Original address: 0x00E73FE4
 * Size: 96 bytes
 *
 * Core truncation function that handles both local and remote files.
 * Checks write permission via FILE_$CHECK_PROT, then calls AST_$TRUNCATE
 * to perform the actual truncation.
 *
 * Assembly analysis:
 *   - link.w A6,-0x4          ; Small stack frame
 *   - Saves A3, A2 to stack
 *   - Calls FILE_$CHECK_PROT with mode=2 (write)
 *   - If status_$ok, calls AST_$TRUNCATE
 *   - Otherwise calls OS_PROC_SHUTWIRED
 */

#include "file/file_internal.h"

/*
 * FILE_$TRUNCATE_D - Truncate a file with domain context
 *
 * Truncates a file to the specified size with explicit domain context
 * for distributed locking. This is the core implementation used by
 * FILE_$TRUNCATE, FILE_$SET_LEN, and FILE_$SET_LEN_D.
 *
 * The function first checks write permission using FILE_$CHECK_PROT
 * with access mode 2 (write). If the check passes, AST_$TRUNCATE is
 * called to perform the actual truncation. If the check fails,
 * OS_PROC_SHUTWIRED is called to release any wired pages.
 *
 * Parameters:
 *   file_uid    - UID of file to truncate
 *   new_size    - Pointer to new size value
 *   domain_ctx  - Pointer to domain context (lock index for distributed locks)
 *   status_ret  - Output status code
 *
 * Status codes:
 *   status_$ok                  - Truncation succeeded
 *   status_$insufficient_rights - No write permission
 *   (other status from AST_$TRUNCATE)
 */
void FILE_$TRUNCATE_D(uid_t *file_uid, uint32_t *new_size,
                      uint32_t *domain_ctx, status_$t *status_ret)
{
    uint16_t rights_out;
    uint8_t result;

    /*
     * Check write permission (mode 2)
     * FILE_$CHECK_PROT signature:
     *   FILE_$CHECK_PROT(uid, access_mask, slot_num, unused, rights_out, status)
     */
    FILE_$CHECK_PROT(file_uid, 2, *domain_ctx, 0, &rights_out, status_ret);

    if (*status_ret == status_$ok) {
        /*
         * Permission granted - perform truncation
         * AST_$TRUNCATE signature:
         *   AST_$TRUNCATE(uid, new_size, flags, result, status)
         */
        AST_$TRUNCATE(file_uid, *new_size, 0, &result, status_ret);
    } else {
        /*
         * Permission denied - release wired pages
         */
        OS_PROC_SHUTWIRED(status_ret);
    }
}

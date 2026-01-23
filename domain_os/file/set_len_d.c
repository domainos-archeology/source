/*
 * FILE_$SET_LEN_D - Set file length with domain context
 *
 * Original address: 0x00E73FB0
 * Size: 26 bytes
 *
 * Sets the length of a file with explicit domain context.
 * This is a simple wrapper around FILE_$TRUNCATE_D that
 * passes all parameters through.
 *
 * Assembly analysis:
 *   - link.w A6,0x0           ; No stack frame
 *   - Pushes all 4 params and calls FILE_$TRUNCATE_D
 */

#include "file/file_internal.h"

/*
 * FILE_$SET_LEN_D - Set file length with domain context
 *
 * Sets the length of a file with explicit domain context for
 * distributed locking. Write permission is required.
 *
 * Parameters:
 *   file_uid    - UID of file to modify
 *   new_length  - Pointer to new length value
 *   domain_ctx  - Pointer to domain context (lock index)
 *   status_ret  - Output status code
 *
 * Calls FILE_$TRUNCATE_D internally.
 */
void FILE_$SET_LEN_D(uid_t *file_uid, uint32_t *new_length,
                     uint32_t *domain_ctx, status_$t *status_ret)
{
    FILE_$TRUNCATE_D(file_uid, new_length, domain_ctx, status_ret);
}

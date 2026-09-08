/*
 * FILE_$TRUNCATE - Truncate a file
 *
 * Original address: 0x00E73FCA
 * Size: 26 bytes
 *
 * Truncates a file to the specified size. This is a wrapper
 * around FILE_$TRUNCATE_D with zero for the domain context.
 *
 * Assembly analysis:
 *   - link.w A6,0x0           ; No stack frame
 *   - Pushes params and calls FILE_$TRUNCATE_D
 *   - Uses pea (-0x28,PC) to push address of constant 0
 */

#include "file/file_internal.h"

/*
 * 0x00E73FAC: the longword of zeroes FILE_$TRUNCATE_D gets as its
 * domain-context argument.  The image holds ONE such cell, shared by
 * FILE_$SET_LEN (`pea (0x12,PC)` at 0x00E73F98) and FILE_$TRUNCATE
 * (0x00E73FD2); it is defined in file/file_data.c as
 * file_$truncate_nil_context.
 */

/*
 * FILE_$TRUNCATE - Truncate a file
 *
 * Truncates a file to the specified size. If the new size is
 * smaller than the current size, data beyond the new size is
 * discarded. Write permission is required.
 *
 * Parameters:
 *   file_uid   - UID of file to truncate
 *   new_size   - Pointer to new size value
 *   status_ret - Output status code
 *
 * Calls FILE_$TRUNCATE_D internally with zero context.
 */
void FILE_$TRUNCATE(uid_t *file_uid, uint32_t *new_size, status_$t *status_ret)
{
    FILE_$TRUNCATE_D(file_uid, new_size, &file_$truncate_nil_context, status_ret);
}

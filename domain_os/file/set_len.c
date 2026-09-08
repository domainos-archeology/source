/*
 * FILE_$SET_LEN - Set file length
 *
 * Original address: 0x00E73F90
 * Size: 26 bytes
 *
 * Sets the length of a file (equivalent to truncate).
 * This is a wrapper around FILE_$TRUNCATE_D with zero for the
 * domain context parameter.
 *
 * Assembly analysis:
 *   - link.w A6,0x0           ; No stack frame
 *   - Pushes params and calls FILE_$TRUNCATE_D
 *   - Uses pea (0x12,PC) to push address of constant 0
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
 * FILE_$SET_LEN - Set file length
 *
 * Sets the length of a file. This is equivalent to truncating or
 * extending a file to the specified size. Write permission is required.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   new_length - Pointer to new length value
 *   status_ret - Output status code
 *
 * Calls FILE_$TRUNCATE_D internally with zero context.
 */
void FILE_$SET_LEN(uid_t *file_uid, uint32_t *new_length, status_$t *status_ret)
{
    FILE_$TRUNCATE_D(file_uid, new_length, &file_$truncate_nil_context, status_ret);
}

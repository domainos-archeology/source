/*
 * DIR_$ROOT_ADDU - Add entry to root directory
 *
 * Adds an entry to the root directory with additional flags.
 * Delegates to the shared add implementation.
 *
 * Original address: 0x00E52B8C
 * Original size: 54 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$ROOT_ADDU - Add entry to root directory
 *
 * This function is similar to DIR_$ADDU but passes additional flags
 * for root directory entries (e.g., node registration flags).
 *
 * Parameters:
 *   dir_uid    - UID of root directory
 *   name       - Name for entry
 *   name_len   - Pointer to name length
 *   file_uid   - UID of file to add
 *   flags      - Pointer to flags
 *   status_ret - Output: status code
 */
void DIR_$ROOT_ADDU(uid_t *dir_uid, char *name, uint16_t *name_len,
                    uid_t *file_uid, uint32_t *flags, status_$t *status_ret)
{
    /* Call shared implementation with provided flags */
    FUN_00e500b8(dir_uid, name, (int16_t)*name_len, file_uid, *flags, status_ret);
}

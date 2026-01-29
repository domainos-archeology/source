/*
 * DIR_$OLD_DELETE_FILEU - Legacy delete file from directory
 *
 * Thin wrapper around FUN_00e56b08 for file deletion.
 *
 * Original address: 0x00E5716E
 * Original size: 64 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_DELETE_FILEU - Legacy delete file from directory
 *
 * Calls the shared delete/drop helper with the appropriate flags
 * extracted from param5 and param4.
 *
 * Parameters:
 *   dir_uid    - UID of parent directory
 *   name       - Name of entry to delete
 *   name_len   - Pointer to name length
 *   param4     - Status output / flags source
 *   param5     - Additional flags source
 *   status_ret - Output: status code
 */
void DIR_$OLD_DELETE_FILEU(uid_t *dir_uid, char *name, uint16_t *name_len,
                           status_$t *param4, void *param5, status_$t *status_ret)
{
    uint8_t buf[8];

    /* Extract flag bytes from params and call shared helper.
     * Assembly reads byte from param5 (0x1c(A6)) and byte from param4 (0x18(A6)).
     * flag3 = 0 for delete file operation. */
    FUN_00e56b08(dir_uid, name, *name_len,
                 *((uint8_t *)param5), *((uint8_t *)param4), 0,
                 buf, status_ret);
}

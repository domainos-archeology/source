/*
 * DIR_$OLD_READ_INFOBLK - Read directory info block
 *
 * Reads the info block data from a directory. The info block is
 * a variable-length data area stored at a fixed offset in the
 * directory header structure.
 *
 * Original address: 0x00E560A6
 * Original size: 150 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_READ_INFOBLK - Read directory info block
 *
 * Acquires the directory lock via FUN_00e54854 with read access
 * (0x10000), then copies the info block data. Checks the directory
 * version (must be < 0x13) before reading.
 *
 * Parameters:
 *   dir_uid    - UID of directory
 *   info_data  - Output: buffer to receive info block data
 *   max_len    - Pointer to maximum buffer length
 *   actual_len - Output: actual length of data read
 *   status_ret - Output: status code
 */
void DIR_$OLD_READ_INFOBLK(uid_t *dir_uid, void *info_data,
                            int16_t *max_len, int16_t *actual_len,
                            status_$t *status_ret)
{
    uint8_t *handle;
    status_$t local_status;
    int16_t len;
    int16_t i;

    /* Acquire directory lock for reading */
    FUN_00e54854(dir_uid, (uint32_t *)&handle, 0x10000, status_ret);
    if (*status_ret == status_$ok) {
        /* Check directory version - must be < 0x13 */
        if (*(uint16_t *)(handle + 4) < 0x13) {
            /* Get the info block length from the directory header */
            *actual_len = *(int16_t *)(handle + 0x37e);

            /* Clamp to max_len */
            if (*max_len < *actual_len) {
                *actual_len = *max_len;
            }

            /* Copy info block data */
            len = *actual_len - 1;
            if (len >= 0) {
                i = 1;
                do {
                    *((uint8_t *)info_data + i - 1) = *(handle + i + 0x381);
                    i++;
                    len--;
                } while (len != (int16_t)-1);
            }

            *status_ret = status_$ok;
        } else {
            *status_ret = status_$naming_illegal_directory_operation;
        }
    }

    /* Release directory lock */
    FUN_00e54734(&local_status);

    /* Exit super mode */
    ACL_$EXIT_SUPER();
}

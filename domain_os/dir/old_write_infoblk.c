/*
 * DIR_$OLD_WRITE_INFOBLK - Write directory info block
 *
 * Writes info block data to a directory. The info block is a
 * variable-length data area stored at a fixed offset in the
 * directory header structure.
 *
 * Original address: 0x00E5613C
 * Original size: 144 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$OLD_WRITE_INFOBLK - Write directory info block
 *
 * Acquires the directory lock via FUN_00e54854 with write access
 * (0x40000), then copies the info block data into the directory.
 * Checks the directory version (must be < 0x13) and data length
 * (must be <= 0x28).
 *
 * Parameters:
 *   dir_uid   - UID of directory
 *   info_data - Input: info block data to write
 *   len       - Pointer to data length
 *   status_ret - Output: status code
 */
void DIR_$OLD_WRITE_INFOBLK(uid_t *dir_uid, void *info_data,
                             int16_t *len, status_$t *status_ret)
{
    uint8_t *handle;
    status_$t local_status;
    int16_t data_len;
    int16_t count;
    int16_t i;

    /* Acquire directory lock for writing */
    FUN_00e54854(dir_uid, (uint32_t *)&handle, 0x40000, status_ret);
    if (*status_ret == status_$ok) {
        /* Check directory version and data length */
        if (*(uint16_t *)(handle + 4) < 0x13 && (data_len = *len, data_len <= 0x28)) {
            /* Write lengths to header */
            *(int16_t *)(handle + 0x37c) = data_len;
            *(int16_t *)(handle + 0x37e) = data_len;

            /* Copy info block data */
            count = data_len - 1;
            if (count >= 0) {
                i = 1;
                do {
                    *(handle + i + 0x381) = *((uint8_t *)info_data + i - 1);
                    i++;
                    count--;
                } while (count != (int16_t)-1);
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

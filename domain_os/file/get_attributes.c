/*
 * FILE_$GET_ATTRIBUTES - Get file attributes (full format)
 *
 * Returns file attributes in full 144-byte format (0x90).
 * Supports checking lock status via flags.
 *
 * Original address: 0x00E5D984
 */

#include "file/file_internal.h"

/*
 * FILE_$GET_ATTRIBUTES
 *
 * Gets file attributes in full format.
 *
 * Parameters:
 *   file_uid   - UID of file
 *   param_2    - Pointer to flags byte (bit 0=check lock, bit 1=check delete, bit 2=skip delete check)
 *   size_ptr   - Pointer to expected buffer size (must be 0x90 = 144)
 *   uid_out    - Output buffer for returned UID (32 bytes = 8 longs)
 *   attr_out   - Output buffer for attributes (144 bytes)
 *   status_ret - Receives operation status
 *
 * Flag bits in param_2[1]:
 *   - Bit 0: Check if file is locked (use flags=1)
 *   - Bit 1: Check delete status
 *   - Bit 2: Skip delete check (use flags=0x21)
 */
void FILE_$GET_ATTRIBUTES(uid_t *file_uid, void *param_2, int16_t *size_ptr,
                          uint32_t *uid_out, void *attr_out, status_$t *status_ret)
{
    status_$t status;
    uid_t local_uid;
    uint32_t attrs[36];  /* 144 bytes = 36 longs */
    uid_t returned_uid;
    uint16_t flags;
    uint8_t result_buf[2];
    uint8_t *flag_bytes = (uint8_t *)param_2;
    int16_t i;

    /* Check size - must be 0x90 (144 bytes) */
    if (*size_ptr != FILE_ATTR_FULL_SIZE) {
        *status_ret = file_$invalid_arg;
        return;
    }

    /* Determine flags based on param_2 */
    if ((flag_bytes[1] & 0x01) != 0) {
        /* Bit 0 set: locked access */
        flags = 0x01;
    } else if ((flag_bytes[1] & 0x04) != 0) {
        /* Bit 2 set: skip delete check, use full access */
        flags = 0x21;
    } else if ((flag_bytes[1] & 0x02) != 0) {
        /* Bit 1 set: check delete status first */
        int8_t result = FILE_$DELETE_INT((uid_t *)(uid_out + 2), 0, result_buf, &status);
        if (result < 0) {
            /* File is locked */
            flags = 0x01;
        } else {
            flags = 0x21;
        }
    } else {
        *status_ret = file_$invalid_arg;
        return;
    }

    /* Copy the file UID to local storage */
    local_uid.high = file_uid->high;
    local_uid.low = file_uid->low;

    /* Get attributes */
    AST_$GET_ATTRIBUTES(&returned_uid, flags, attrs, &status);

    /* Copy 36 longs of attributes to output */
    for (i = 0; i < 36; i++) {
        ((uint32_t *)attr_out)[i] = attrs[i];
    }

    /* Copy 8 longs of UID info to output */
    for (i = 0; i < 8; i++) {
        uid_out[i] = ((uint32_t *)&returned_uid)[i % 2];
    }

    *status_ret = status;
}

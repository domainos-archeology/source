/*
 * dir_$old_read_link_data - Read link target data from overflow blocks
 *
 * Reads symbolic link target data from overflow blocks referenced
 * by the link descriptor stored in the directory entry. The descriptor
 * (passed as link_desc) has the layout:
 *   - uint16_t total_len    (total link data bytes)
 *   - uint16_t block_idx[3] (up to 3 overflow block indices)
 *
 * Each overflow block holds up to 0x90 bytes of link data starting
 * at offset 0x370 (= block_idx * 0x96 + 0x370) within the directory
 * buffer. The function iterates through blocks, copying data into
 * the output buffer until total_len bytes have been transferred.
 *
 * Note: Callers pass a 5th parameter (status_ret) which is unused
 * by this function. The declaration uses 4 parameters.
 *
 * Parameters:
 *   handle    - Base of mapped directory buffer
 *   link_desc - Pointer to link descriptor (at entry + 0x28)
 *   buf       - Output buffer for link target data
 *   buf_len   - Output: actual length written (set to total_len)
 *
 * Original address: 0x00E55764
 * Size: 112 bytes
 */

#include "dir/dir_internal.h"

void dir_$old_read_link_data(uint32_t handle, void *link_desc,
                             uint8_t *buf, uint16_t *buf_len)
{
    char *base = (char *)(uintptr_t)handle;
    uint16_t *desc = (uint16_t *)link_desc;
    uint16_t total_len;
    uint16_t bytes_copied = 0;
    int16_t block_num;
    int16_t entry_num;

    /* Copy total length to output */
    total_len = desc[0];
    *buf_len = total_len;

    /* Iterate through up to 3 overflow blocks */
    for (entry_num = 1, block_num = 0; block_num < 3; block_num++, entry_num++) {
        uint16_t block_idx = desc[1 + block_num];
        char *block_data = base + (uint32_t)block_idx * 0x96;
        uint16_t byte_pos;

        /* Copy up to 0x90 bytes from this block */
        for (byte_pos = 1; byte_pos <= 0x90; byte_pos++) {
            /* Destination: buf + (entry_num-1)*0x90 + byte_pos - 1 */
            buf[(entry_num - 1) * 0x90 + byte_pos - 1] =
                *(uint8_t *)(block_data + byte_pos + 0x36f);

            bytes_copied++;
            if (bytes_copied == total_len) {
                return;
            }
        }
    }
}

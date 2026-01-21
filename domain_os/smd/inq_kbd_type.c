/*
 * smd/inq_kbd_type.c - SMD_$INQ_KBD_TYPE implementation
 *
 * Inquires the keyboard type.
 *
 * Original address: 0x00E6E122
 *
 * This function wraps KBD_$INQ_KBD_TYPE and performs some post-processing
 * on the result to handle special keyboard type encodings.
 *
 * Assembly analysis:
 * 1. Calls KBD_$INQ_KBD_TYPE with a local buffer
 * 2. Copies result to caller's buffer (up to min of buffer sizes)
 * 3. Special case: if length == 2 and second char is '@', sets length to 1
 *    and replaces '@' with space (0x20)
 * 4. Otherwise: converts second char to lowercase (& 0x1F + 0x60)
 */

#include "smd/smd_internal.h"
#include "kbd/kbd.h"

/* Internal KBD device reference */
extern uint32_t SMD_KBD_DEVICE;  /* at 0x00E6D92C */

/*
 * SMD_$INQ_KBD_TYPE - Inquire keyboard type
 *
 * Returns the keyboard type string for the current display.
 * Wraps KBD_$INQ_KBD_TYPE with additional processing.
 *
 * Parameters:
 *   buf_size   - Pointer to buffer size (input/output)
 *   buffer     - Output buffer for keyboard type string
 *   length     - Output: actual length of keyboard type
 *   status_ret - Output: status return
 *
 * Returns:
 *   status_$ok on success
 *   status_$display_invalid_buffer_size if buffer too small
 */
void SMD_$INQ_KBD_TYPE(uint16_t *buf_size, uint8_t *buffer, uint16_t *length,
                       status_$t *status_ret)
{
    uint8_t local_buf[120];  /* Local buffer for KBD call */
    uint16_t result_len;
    uint16_t copy_len;
    int16_t i;

    /* Call KBD_$INQ_KBD_TYPE to get the keyboard type */
    KBD_$INQ_KBD_TYPE(&SMD_KBD_DEVICE, local_buf, length, status_ret);

    if (*status_ret != status_$ok) {
        return;
    }

    /* Determine how much to copy */
    result_len = *length;
    copy_len = result_len;
    if (*buf_size < result_len) {
        copy_len = *buf_size;
    }

    /* Copy result to caller's buffer */
    if (copy_len > 0) {
        for (i = 0; i < copy_len; i++) {
            buffer[i] = local_buf[i + 1];  /* Skip first byte */
        }
    }

    /* Check if buffer was too small */
    if (*buf_size < result_len) {
        *status_ret = status_$display_invalid_buffer_size;
    }

    /* Special keyboard type processing */
    if (result_len == 2) {
        if (buffer[1] == '@') {
            /* Special case: '@' -> convert to single space */
            *length = 1;
            buffer[1] = 0x20;  /* Space */
        } else {
            /* Convert second char to lowercase */
            buffer[1] = (buffer[1] & 0x1F) + 0x60;
        }
    }
}

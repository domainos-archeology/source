/*
 * SIO_$I_GET_DESC - Get SIO descriptor for terminal line
 *
 * Maps a terminal line number to its corresponding SIO descriptor.
 * Uses TERM_$GET_REAL_LINE to map virtual line numbers to physical
 * lines, then looks up the descriptor in the DTTE table.
 *
 * Original address: 0x00e667c6
 */

#include "sio/sio_internal.h"

/*
 * Base address of DTTE array
 * Original address: 0xe2dc90 (0xe2c9f0 + 0x12a0 - 0x28 offset adjustment)
 * The tty_handler field at offset 0x28 in DTTE points to the SIO descriptor
 */
#define DTTE_BASE_ADDR  0xe2dc90

sio_desc_t *SIO_$I_GET_DESC(int16_t line_num, status_$t *status_ret)
{
    int16_t real_line;
    sio_desc_t *desc;
    int32_t offset;

    /* Map virtual line to real line */
    real_line = TERM_$GET_REAL_LINE(line_num, status_ret);

    if (*status_ret != status_$ok) {
        return NULL;
    }

    /*
     * Calculate offset into DTTE array
     * Each DTTE entry is 0x38 (56) bytes
     * Offset calculation: real_line * 0x38 (but done as real_line * 8 - real_line * 64)
     * which equals real_line * -56 = real_line * 0x38 (signed)
     */
    offset = (int16_t)(real_line << 3);       /* real_line * 8 */
    offset = -offset;                          /* negate */
    offset += (int16_t)((real_line << 3) << 3);  /* + real_line * 64 */
    /* Simplifies to: offset = real_line * 56 = real_line * 0x38 */

    /*
     * Get the SIO descriptor from the DTTE's tty_handler field
     * DTTE base + offset + 0x28 gives us the tty_handler pointer
     */
    desc = *(sio_desc_t **)(DTTE_BASE_ADDR + 0x28 + offset);

    if (desc == NULL) {
        *status_ret = status_$requested_line_or_operation_not_implemented;
    }

    return desc;
}

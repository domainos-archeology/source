/*
 * TTY_$I_PUT_OUTPUT - Write characters to TTY output with input check
 *
 * Wrapper around tty_$i_put_chars that first checks if output should
 * be deferred because there is pending input. When the "defer output
 * while input pending" flag (input_flags bit 6, at offset 0x17) is
 * set and the input buffer is not empty (tail != head), instead of
 * writing, it sets the TTY_STATUS_INPUT_WAIT flag and returns 0.
 *
 * This prevents output from interleaving with user typing.
 *
 * Parameters:
 *   tty   - TTY descriptor
 *   buf   - Character buffer to output
 *   count - Number of characters (low 16 bits)
 *   max   - Maximum to process (high 16 bits passed to put_chars)
 *
 * Returns:
 *   Number of characters actually written, or 0 if deferred
 *
 * Original address: 0x00e1bf0e
 * Size: 98 bytes
 */

#include "tty/tty_internal.h"

int16_t TTY_$I_PUT_OUTPUT(tty_desc_t *tty, void *buf, uint16_t count, uint16_t max)
{
    ml_$spin_token_t token;

    if ((*(uint8_t *)((char *)tty + 0x17) & 0x40) != 0 &&
        tty->input_tail != tty->input_head) {
        /* Input pending and defer-output flag set: defer the write */
        token = ML_$SPIN_LOCK(&TTY_$SPIN_LOCK);
        *(uint8_t *)((char *)tty + 0x09) |= TTY_STATUS_INPUT_WAIT;
        ML_$SPIN_UNLOCK(&TTY_$SPIN_LOCK, token);
        return 0;
    }

    /* No deferral: write directly */
    return (int16_t)tty_$i_put_chars(tty, (const uint8_t *)buf,
                                     ((uint32_t)count << 16) | (uint32_t)max);
}

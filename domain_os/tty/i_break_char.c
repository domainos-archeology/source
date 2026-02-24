/*
 * TTY_$I_BREAK_CHAR - Process a break/newline character
 *
 * Handles characters that terminate a line of input (break chars,
 * newlines, CR). Stores the character in the input buffer, echoes
 * it if echo is enabled, updates the head pointer to mark the line
 * as complete, and advances the input eventcount to wake any reader.
 *
 * Also handles:
 *   - Signal delivery if TTY_STATUS_SIG_PEND is set
 *   - Output wait release if TTY_STATUS_INPUT_WAIT is set
 *   - Saved input flags update
 *
 * Parameters:
 *   tty - TTY descriptor
 *   ch  - The break/newline character
 *
 * Original address: 0x00e1b8b0
 * Size: 122 bytes
 */

#include "tty/tty_internal.h"

void TTY_$I_BREAK_CHAR(tty_desc_t *tty, uint8_t ch)
{
    /* Insert the break character into the input buffer */
    tty_$i_buf_insert(ch, &tty->input_read);

    /* Echo the character if echo is enabled */
    if ((*(uint8_t *)((char *)tty + 0x17) & 0x01) != 0) {
        TTY_$I_XMIT_CHAR(tty, (uint16_t)ch << 8);
    }

    /* Mark the line as complete: advance head to tail */
    tty->input_head = tty->input_tail;

    /* Save current column position */
    tty->saved_input_flags = tty->column;

    /* Advance the input eventcount to wake readers */
    TTY_$I_ADVANCE_EC(tty->input_ec);

    /* If signal pending on input, send SIGWINCH */
    if ((*(uint8_t *)((char *)tty + 0x09) & TTY_STATUS_SIG_PEND) != 0) {
        TTY_$I_SIGNAL(tty, TTY_SIG_WINCH);
    }

    /* If output wait is pending, clear it and advance output EC */
    if ((*(uint8_t *)((char *)tty + 0x09) & TTY_STATUS_INPUT_WAIT) != 0) {
        *(uint8_t *)((char *)tty + 0x09) &= ~TTY_STATUS_INPUT_WAIT;
        TTY_$I_ADVANCE_EC(tty->output_ec);
    }
}

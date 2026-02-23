/*
 * TTY_$I_KILL_LINE - Kill (erase) the entire input line
 *
 * Erases the current input line from the buffer. Two modes:
 *
 * If echo_flags bit 2 (CRT kill) is set:
 *   Repeatedly calls TTY_$I_DELETE_CHAR to erase characters
 *   one at a time until the buffer is back to the head position.
 *
 * If echo_flags bit 2 is clear:
 *   Echoes the kill character (func_chars[2], offset 0x26),
 *   optionally outputs a newline (if echo_flags bit 5 is set),
 *   and then resets the tail pointer. If tail == read position
 *   (nothing beyond the canonical boundary), just resets head.
 *   Otherwise resets tail back to head position.
 *
 * Parameters:
 *   tty - TTY descriptor
 *
 * Original address: 0x00e1b6ac
 * Size: 106 bytes
 */

#include "tty/tty_internal.h"

void TTY_$I_KILL_LINE(tty_desc_t *tty)
{
    int16_t tail;

    if ((*(uint8_t *)((char *)tty + 0x1F) & 0x04) != 0) {
        /* CRT kill mode: delete chars one at a time */
        while (tty->input_tail != tty->input_head) {
            TTY_$I_DELETE_CHAR(tty);
        }
    } else {
        /* Non-CRT kill: echo kill char, maybe newline, then reset buffer */
        TTY_$I_ECHO_CHAR(tty, tty->func_chars[2]);

        if ((*(uint8_t *)((char *)tty + 0x1F) & 0x20) != 0) {
            TTY_$I_NEWLINE(tty);
        }

        tail = tty->input_tail;
        if (tail == tty->input_read) {
            /* Nothing beyond canonical boundary; if not at head, reset head */
            if (tail != tty->input_head) {
                tty->input_head = tail;
            }
        } else {
            /* Reset tail back to head (discard all pending input) */
            tty->input_tail = tty->input_head;
        }
    }
}

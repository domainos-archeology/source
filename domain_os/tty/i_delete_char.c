/*
 * TTY_$I_DELETE_CHAR - Delete the last character from the input buffer
 *
 * Removes the most recently typed character from the input buffer
 * and handles the visual feedback (echo) depending on echo mode:
 *
 * Echo modes (echo_flags at offset 0x1F):
 *   - Bit 0 (CRT erase): Use BS/SP/BS sequence to erase on screen
 *   - Bit 1 (CRT BS): Also erase with BS before the BS/SP/BS
 *   - Bit 3 (echo erase): Echo the deleted char with backslash prefix
 *   - Neither: Just echo the erase character (func_chars[0], the DEL char)
 *
 * For TAB characters, calculates the actual column width by calling
 * TTY_$I_CALC_COLUMN to determine how many BS sequences to send.
 *
 * For control characters displayed as ^X (2 columns), sends 2 erase
 * sequences.
 *
 * When the buffer becomes empty and output wait is pending, clears
 * the wait flag and advances the output eventcount.
 *
 * Parameters:
 *   tty - TTY descriptor
 *
 * Original address: 0x00e1b538
 * Size: 372 bytes
 */

#include "tty/tty_internal.h"

void TTY_$I_DELETE_CHAR(tty_desc_t *tty)
{
    int16_t tail;
    int16_t erase_count;
    uint8_t ch;
    uint16_t col;

    tail = tty->input_tail;

    /* Nothing to delete if buffer is empty */
    if (tail == tty->input_head) {
        return;
    }

    /* If tail == read position, just reset head to tail (empty canonical buffer) */
    if (tail == tty->input_read) {
        tty->input_head = tail;
        return;
    }

    /* Decrement tail (circular: 1 wraps to 0x100) */
    if (tail == 1) {
        tty->input_tail = 0x100;
    } else {
        tty->input_tail = tty->input_tail - 1;
    }

    /* If buffer is now empty and output wait is pending, clear it */
    if (tty->input_tail == tty->input_head) {
        if ((*(uint8_t *)((char *)tty + 0x09) & TTY_STATUS_INPUT_WAIT) != 0) {
            *(uint8_t *)((char *)tty + 0x09) &= ~TTY_STATUS_INPUT_WAIT;
            TTY_$I_ADVANCE_EC(tty->output_ec);
        }
    }

    /* If echo is not enabled, nothing more to do */
    if ((*(uint8_t *)((char *)tty + 0x17) & 0x01) == 0) {
        return;
    }

    /* Get the character we just deleted */
    ch = tty->input_buffer[tty->input_tail];

    if (ch == 0x09) {
        /* TAB: calculate how many columns it occupied */
        col = TTY_$I_CALC_COLUMN(&tty->input_read, tty->input_head,
                                 tty->saved_input_flags, tty->echo_flags);
        erase_count = 8 - (col & 7);
    } else if (ch < 0x20 || ch == 0x7F) {
        /* Control character: displayed as ^X so takes 2 columns */
        if ((*(uint8_t *)((char *)tty + 0x1F) & 0x10) == 0) {
            /* Control chars not echoed, nothing to erase */
            return;
        }
        erase_count = 2;
    } else {
        /* Normal character: 1 column */
        erase_count = 1;
    }

    /* Check echo mode */
    if ((*(uint8_t *)((char *)tty + 0x1F) & 0x01) != 0) {
        /* CRT erase mode: send BS (and optionally space+BS) sequences */
        if (erase_count != 0) {
            int16_t i = erase_count - 1;
            do {
                if ((*(uint8_t *)((char *)tty + 0x1F) & 0x02) != 0) {
                    /* CRT BS mode: send BS then space */
                    TTY_$I_XMIT_CHAR(tty, 0x0800);   /* BS */
                    TTY_$I_XMIT_CHAR(tty, 0x2000);   /* SP */
                }
                TTY_$I_XMIT_CHAR(tty, 0x0800);       /* BS */
                i = i - 1;
            } while (i != -1);
        }
    } else if ((*(uint8_t *)((char *)tty + 0x1F) & 0x08) != 0) {
        /* Echo erase mode: prefix with backslash, then echo char */

        /* If not in "already echoing erase" state, output backslash */
        if ((int8_t)*(uint8_t *)((char *)tty + 0x09) >= 0) {
            TTY_$I_XMIT_CHAR(tty, 0x5C00);   /* backslash '\' */
        }

        if (ch < 0x20 || ch == 0x7F) {
            /* Control character: echo as ^X */
            TTY_$I_XMIT_CHAR(tty, 0x5E00);   /* '^' */
            if (ch == 0x7F) {
                TTY_$I_XMIT_CHAR(tty, 0x3F00);   /* '?' for DEL */
            } else {
                TTY_$I_XMIT_CHAR(tty, (uint16_t)(ch + 0x40) << 8);  /* control char + 0x40 */
            }
        } else {
            /* Normal character: echo it */
            TTY_$I_XMIT_CHAR(tty, (uint16_t)ch << 8);
        }

        /* Set the "echoing erase" flag (bit 7 of status byte at 0x09) */
        *(uint8_t *)((char *)tty + 0x09) |= 0x80;
    } else {
        /* Default: just echo the erase character (func_chars[0]) */
        TTY_$I_XMIT_CHAR(tty, (uint16_t)tty->func_chars[0] << 8);
    }
}

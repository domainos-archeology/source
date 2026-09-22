/*
 * TTY_$I_KILL_LINE - Erase the whole pending input line
 *
 * 0x00E1B6AC..0x00E1B714 (106 bytes), A2 = tty.  Only caller: TTY_$I_RCV
 * (0x00E1BB02), for character class 8.
 *
 *   0x00E1B6B6  echo_flags bit 2 (btst.b #2,(0x1f,A2)) set:
 *                 while input_tail != input_head: TTY_$I_DELETE_CHAR(tty)
 *                 (0x00E1B6C8 test first, 0x00E1B6C0 body)
 *   0x00E1B6D4  else: TTY_$I_ECHO_CHAR(tty, func_chars[2]) (byte (0x26,A2))
 *   0x00E1B6E2    echo_flags bit 5 -> TTY_$I_NEWLINE(tty)
 *   0x00E1B6F2    tail == input_read: tail != input_head -> input_head = tail
 *   0x00E1B708    tail != input_read: input_tail = input_head
 *
 * Original address: 0x00e1b6ac
 * Size: 106 bytes
 */

#include "tty/tty_internal.h"

void TTY_$I_KILL_LINE(tty_desc_t *tty)
{
    uint16_t tail;

    if ((tty->echo_flags & 0x00000004) != 0) {             /* 0x00E1B6B6 */
        while (tty->input_tail != tty->input_head) {       /* 0x00E1B6C8 */
            TTY_$I_DELETE_CHAR(tty);                       /* 0x00E1B6C2 */
        }
        return;
    }

    TTY_$I_ECHO_CHAR(tty, tty->func_chars[2]);             /* 0x00E1B6D4 */

    if ((tty->echo_flags & 0x00000020) != 0) {             /* 0x00E1B6E2 */
        TTY_$I_NEWLINE(tty);
    }

    tail = tty->input_tail;                                /* 0x00E1B6F2 */
    if (tail == tty->input_read) {
        if (tail != tty->input_head) {                     /* 0x00E1B6FC */
            tty->input_head = tail;                        /* 0x00E1B702 */
        }
    } else {
        tty->input_tail = tty->input_head;                 /* 0x00E1B708 */
    }
}

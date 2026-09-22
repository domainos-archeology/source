/*
 * TTY_$I_DELETE_CHAR - Delete the last character from the input buffer
 *
 * Backs the input producer position up one character and, when input echo
 * is on, erases it visually according to echo_flags:
 *   bit 0 (CRT erase):   BS per column (BS SP BS per column when bit 1 is on)
 *   bit 3 (echo erase):  "\" once (until state bit 7 says one was sent), then
 *                        the character itself (^X / ^? for controls / DEL)
 *   neither:             the erase function character, func_chars[0]
 *
 * 0x00E1B538..0x00E1B6AA (372 bytes), A2 = tty:
 *   0x00E1B544  tail == input_head (0x2ca) -> return
 *   0x00E1B550  tail == input_read (0x2cc) -> input_head = tail; return
 *   0x00E1B55E  tail == 1 ? 0x100 : tail - 1   -> input_tail (0x2ce)
 *   0x00E1B570  tail == input_head and state bit 1 -> clear, ADVANCE_EC(output_ec)
 *   0x00E1B592  input_flags bit 0 (echo) clear -> return
 *   0x00E1B59C  ch = (0x2d1,A2,tail) = input_buffer[tail - 1]
 *   0x00E1B5A8  TAB: cols = 8 - (CALC_COLUMN(&input_read, input_head,
 *                    saved_input_flags, echo_flags) & 7)
 *   0x00E1B5D0  control / DEL: echo_flags bit 4 clear -> return, else 2
 *   0x00E1B5EA  otherwise 1
 *   0x00E1B5EC  echo_flags bit 0: cols == 0 -> return; dbf loop cols times:
 *                    bit 1 -> XMIT(BS), XMIT(SP); then XMIT(BS)
 *   0x00E1B636  echo_flags bit 3: state byte 0x09 bpl -> XMIT('\');
 *                    control/DEL -> XMIT('^') then XMIT(ch+0x40) or XMIT('?');
 *                    else XMIT(ch); then bset.b #7,(0x9,A2)
 *   0x00E1B696  else XMIT(func_chars[0])
 * Bytes (0x9,A2) / (0x17,A2) / (0x1f,A2) are the low bytes of state_flags /
 * input_flags / echo_flags, so the bit numbers carry over unchanged.
 *
 * Callers: TTY_$I_RCV 0x00E1BADA, TTY_$I_KILL_LINE 0x00E1B6C2,
 * TTY_$I_WORD_ERASE 0x00E1B758 / 0x00E1B796.
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
    uint16_t tail;
    int16_t erase_count;
    int16_t i;
    uint8_t ch;
    uint16_t col;

    tail = tty->input_tail;                                /* 0x00E1B544 */

    /* 0x00E1B548: nothing pending */
    if (tail == tty->input_head) {
        return;
    }

    /* 0x00E1B550: tail == read position -> just commit and leave */
    if (tail == tty->input_read) {
        tty->input_head = tail;
        return;
    }

    /* 0x00E1B55E: back the tail up one, wrapping 1 -> 0x100 */
    if (tail == 1) {
        tty->input_tail = 0x100;
    } else {
        tty->input_tail = (uint16_t)(tty->input_tail - 1);
    }

    /* 0x00E1B570: buffer now empty and a writer waiting on input -> wake it */
    if (tty->input_tail == tty->input_head) {
        if ((tty->state_flags & TTY_STATUS_INPUT_WAIT) != 0) {
            tty->state_flags &= (uint16_t)~TTY_STATUS_INPUT_WAIT;
            TTY_$I_ADVANCE_EC(tty->output_ec);
        }
    }

    /* 0x00E1B592: no echo -> nothing to erase */
    if ((tty->input_flags & 0x00000001) == 0) {
        return;
    }

    /* 0x00E1B59C: the character being deleted */
    ch = tty->input_buffer[tty->input_tail - 1];

    if (ch == 0x09) {                                      /* 0x00E1B5A8 */
        col = TTY_$I_CALC_COLUMN(&tty->input_read, (int16_t)tty->input_head,
                                 tty->saved_input_flags, tty->echo_flags);
        erase_count = (int16_t)(8 - (col & 7));            /* 0x00E1B5C6..0x00E1B5CC */
    } else if (ch < 0x20 || ch == 0x7f) {                  /* 0x00E1B5D0 */
        if ((tty->echo_flags & 0x00000010) == 0) {         /* 0x00E1B5DC */
            return;
        }
        erase_count = 2;
    } else {
        erase_count = 1;                                   /* 0x00E1B5EA */
    }

    if ((tty->echo_flags & 0x00000001) != 0) {             /* 0x00E1B5EC */
        /* CRT erase */
        if (erase_count == 0) {                            /* 0x00E1B5F4 */
            return;
        }
        i = (int16_t)(erase_count - 1);                    /* 0x00E1B5FA dbf count */
        do {
            if ((tty->echo_flags & 0x00000002) != 0) {     /* 0x00E1B5FE */
                TTY_$I_XMIT_CHAR(tty, 0x08);               /* BS */
                TTY_$I_XMIT_CHAR(tty, 0x20);               /* SP */
            }
            TTY_$I_XMIT_CHAR(tty, 0x08);                   /* 0x00E1B622 BS */
            i--;
        } while (i != -1);                                 /* 0x00E1B630 dbf */
        return;
    }

    if ((tty->echo_flags & 0x00000008) != 0) {             /* 0x00E1B636 */
        /* echo-erase: one backslash per erase run (state bit 7 latches it) */
        if ((int8_t)(tty->state_flags & 0xff) >= 0) {      /* 0x00E1B63E tst.b / bmi */
            TTY_$I_XMIT_CHAR(tty, 0x5c);                   /* '\' */
        }
        if (ch < 0x20 || ch == 0x7f) {                     /* 0x00E1B652 */
            TTY_$I_XMIT_CHAR(tty, 0x5e);                   /* '^' */
            if (ch != 0x7f) {                              /* 0x00E1B66C */
                TTY_$I_XMIT_CHAR(tty, (uint8_t)(0x40 + ch));
            } else {
                TTY_$I_XMIT_CHAR(tty, 0x3f);               /* '?' */
            }
        } else {
            TTY_$I_XMIT_CHAR(tty, ch);                     /* 0x00E1B684 */
        }
        tty->state_flags |= 0x0080;                        /* 0x00E1B68E bset.b #7,(0x9,A2) */
        return;
    }

    /* 0x00E1B696: plain -- echo the erase function character */
    TTY_$I_XMIT_CHAR(tty, tty->func_chars[0]);
}

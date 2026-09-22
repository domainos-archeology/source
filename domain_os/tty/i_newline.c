/*
 * TTY_$I_NEWLINE - Output a newline sequence
 *
 * 0x00E1B456..0x00E1B4B0 (92 bytes), A2 = tty.  Two booleans start true
 * (st D0b; move.b D0b,D2b): D0 = "send LF", D2 = "send CR".
 *   0x00E1B466  output_flags bit 0 (btst.b #0,(0xf,A2)): D0 = 0, skip to
 *               the CR test (D2 still true)
 *   0x00E1B472  else output_flags bit 1: D2 = 0
 *   0x00E1B47C  D0 true -> tty_$i_put_chars(tty, LF @0x00E1B4B2, 0x1000C)
 *   0x00E1B494  D2 true -> tty_$i_put_chars(tty, CR @0x00E1B4B4, 0x1000C)
 * Image bytes at 0x00E1B4B2: 0a 00 0d 00.  So: bit 0 -> CR only;
 * bit 1 -> LF only; neither -> LF then CR; both -> CR only.
 *
 * Callers: TTY_$I_KILL_LINE 0x00E1B6EC, TTY_$I_RCV 0x00E1BC04.
 *
 * Original address: 0x00e1b456
 * Size: 92 bytes
 */

#include "tty/tty_internal.h"

static const uint8_t tty_$newline_lf = 0x0a;   /* 0x00E1B4B2 */
static const uint8_t tty_$newline_cr = 0x0d;   /* 0x00E1B4B4 */

#define TTY_OUTPUT_FLAGS  0x1000c

void TTY_$I_NEWLINE(tty_desc_t *tty)
{
    boolean send_lf = true;                                /* D0b */
    boolean send_cr = true;                                /* D2b */

    if ((tty->output_flags & 0x00000001) != 0) {           /* 0x00E1B466 */
        send_lf = false;
    } else if ((tty->output_flags & 0x00000002) != 0) {    /* 0x00E1B472 */
        send_cr = false;
    }

    if (send_lf < 0) {                                     /* 0x00E1B47C tst.b / bpl */
        tty_$i_put_chars(tty, &tty_$newline_lf, TTY_OUTPUT_FLAGS);
    }
    if (send_cr < 0) {                                     /* 0x00E1B494 */
        tty_$i_put_chars(tty, &tty_$newline_cr, TTY_OUTPUT_FLAGS);
    }
}

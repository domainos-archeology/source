/*
 * TTY_$I_XMIT_CHAR - Transmit a single character
 *
 * Outputs one character through tty_$i_put_chars (0x00E1B00A).
 *
 * 0x00E1B3B4..0x00E1B3CC (26 bytes):
 *   move.l #0x1000c,-(SP)    ; flags: count 0x0C? no -- see below
 *   pea (0xc,A6)             ; &ch: the byte the caller pushed with move.b
 *   move.l (0x8,A6),-(SP)    ; tty
 *   bsr.w tty_$i_put_chars
 * The 32-bit literal 0x0001000C is tty_$i_put_chars' third argument as one
 * longword (high word 1 = count of one character, low word 0x0C = the
 * output-mode bits); see tty_$i_put_chars for how it splits them.
 *
 * Parameters:
 *   tty - TTY descriptor
 *   ch  - Character to transmit (a byte; its address is what is passed on)
 *
 * Original address: 0x00e1b3b4
 * Size: 26 bytes
 */

#include "tty/tty_internal.h"

/* Third argument of tty_$i_put_chars: 0x0001000C (0x00E1B3B8) */
#define TTY_XMIT_FLAGS  0x1000c

void TTY_$I_XMIT_CHAR(tty_desc_t *tty, uint8_t ch)
{
    tty_$i_put_chars(tty, &ch, TTY_XMIT_FLAGS);          /* 0x00E1B3B8..0x00E1B3C6 */
}

/*
 * TTY_$I_ECHO_CHAR - Echo a character to the TTY
 *
 * Echoes one input character through tty_$i_put_chars (0x00E1B00A).  TAB,
 * LF and printable characters go out as themselves; other control
 * characters and DEL go out as themselves unless echo_flags bit 4
 * (echo control characters visibly) is set, in which case they become
 * "^" followed by ch + 0x40, or "^?" for DEL.
 *
 * 0x00E1B3CE..0x00E1B450 (132 bytes), A2 = tty, D2b = ch from (0xc,A6):
 *   0x00E1B3DE  cmpi.b #0x9 / #0xa            TAB, LF -> plain
 *   0x00E1B3EA  cmpi.b #0x20 bcs / #0x7f bne  printable (0x20..0x7E) -> plain
 *   0x00E1B3F6  btst.b #4,(0x1f,A2)           echo_flags bit 4 clear -> plain
 *   0x00E1B3FE  put_chars(tty, "^" @0x00E1B452, 0x1000C)
 *   0x00E1B412  cmpi.b #0x7f: DEL -> put_chars(tty, "?" @0x00E1B454, 0x1000C)
 *               else (-0x6,A6) = ch + 0x40 -> put_chars(tty, &local, 0x1000C)
 *   0x00E1B438  plain: put_chars(tty, &(0xc,A6), 0x1000C)
 * Image bytes at 0x00E1B452: 5e 00 3f 00.
 *
 * Parameters:
 *   tty - TTY descriptor
 *   ch  - Character to echo (byte in the high half of the word slot)
 *
 * Original address: 0x00e1b3ce
 * Size: 132 bytes
 */

#include "tty/tty_internal.h"

static const uint8_t tty_$echo_caret = 0x5e;   /* 0x00E1B452: "^" */
static const uint8_t tty_$echo_query = 0x3f;   /* 0x00E1B454: "?" */

/* Third argument of tty_$i_put_chars: one character, mode 0x0C */
#define TTY_OUTPUT_FLAGS  0x1000c

void TTY_$I_ECHO_CHAR(tty_desc_t *tty, uint8_t ch)
{
    uint8_t ctrl_ch;                                    /* (-0x6,A6) */

    /* 0x00E1B3DE..0x00E1B3FC: which characters are echoed verbatim */
    if (ch == 0x09 || ch == 0x0a ||
        (ch >= 0x20 && ch != 0x7f) ||
        (tty->echo_flags & 0x00000010) == 0) {
        tty_$i_put_chars(tty, &ch, TTY_OUTPUT_FLAGS);   /* 0x00E1B438 */
        return;
    }

    /* 0x00E1B3FE: caret prefix */
    tty_$i_put_chars(tty, &tty_$echo_caret, TTY_OUTPUT_FLAGS);

    if (ch == 0x7f) {                                   /* 0x00E1B412 */
        tty_$i_put_chars(tty, &tty_$echo_query, TTY_OUTPUT_FLAGS);   /* 0x00E1B42C */
    } else {
        ctrl_ch = (uint8_t)(0x40 + ch);                 /* 0x00E1B41E..0x00E1B422 */
        tty_$i_put_chars(tty, &ctrl_ch, TTY_OUTPUT_FLAGS);          /* 0x00E1B426 */
    }
}

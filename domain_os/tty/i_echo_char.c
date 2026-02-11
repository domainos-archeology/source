/*
 * TTY_$I_ECHO_CHAR - Echo a character to the TTY
 *
 * Echoes a character to the terminal output. Control characters
 * (except TAB and LF) are displayed with a caret prefix (e.g., ^C).
 * DEL (0x7F) is displayed as "^?".
 *
 * Parameters:
 *   tty - TTY descriptor
 *   ch  - Character to echo
 *
 * Original address: 0x00e1b3ce
 * Size: 132 bytes
 */

#include "tty/tty_internal.h"

/* String constants for control character display */
static const char caret_str[] = "^";   /* 0xe1b452 */
static const char del_str[] = "?";     /* 0xe1b454 */

/* Output flags for tty_$i_put_chars */
#define TTY_OUTPUT_FLAGS  0x1000c

void TTY_$I_ECHO_CHAR(tty_desc_t *tty, uint8_t ch)
{
    char ctrl_buf[6];
    const char *output_str;
    uint8_t orig_ch = ch;

    /*
     * Check if character needs special handling:
     * - TAB (9), LF (10): output normally
     * - >= 0x20 (space) and != 0x7F (DEL): output normally
     * - Otherwise: if echo control chars flag is set, show with caret
     */
    if (ch == 9 || ch == 10 ||
        (ch >= 0x20 && ch != 0x7F) ||
        (*(uint8_t *)((char *)tty + 0x1f) & 0x10) == 0) {
        /* Output character directly */
        output_str = (const char *)&ch;
    } else {
        /* Control character - output caret prefix first */
        tty_$i_put_chars(tty, caret_str, TTY_OUTPUT_FLAGS);

        if (orig_ch == 0x7F) {
            /* DEL character - display as ^? */
            output_str = del_str;
        } else {
            /* Control character - display as ^X where X = ch + 0x40 */
            ctrl_buf[0] = orig_ch + 0x40;
            output_str = ctrl_buf;
        }
    }

    /* Output the character/string */
    tty_$i_put_chars(tty, output_str, TTY_OUTPUT_FLAGS);
}

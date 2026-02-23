/*
 * TTY_$I_NEWLINE - Output a newline sequence to the TTY
 *
 * Outputs a CR/LF or LF newline sequence depending on the TTY's
 * output flags (offset 0x0F in the descriptor):
 *   - Bit 0 set: output nothing (suppress newline)
 *   - Bit 1 set: output only LF (no CR)
 *   - Otherwise: output CR+LF
 *
 * Uses tty_$i_put_chars with flags=0x1000c (count=12 in high word?
 * Actually: high 16 bits = 0x0001, low 16 = 0x000c; this encodes
 * the put_chars mode and a small count).
 *
 * The data bytes are inline constants:
 *   0x0A (LF) at offset used for LF-only
 *   0x0D (CR) at offset used for CR
 *
 * Parameters:
 *   tty - TTY descriptor
 *
 * Original address: 0x00e1b456
 * Size: 92 bytes
 */

#include "tty/tty_internal.h"

/* Inline constant bytes (PC-relative in original code) */
static const uint8_t newline_lf[] = { 0x0A };
static const uint8_t newline_cr[] = { 0x0D };

void TTY_$I_NEWLINE(tty_desc_t *tty)
{
    uint8_t output_flags = *(uint8_t *)((char *)tty + 0x0F);
    char do_cr = -1;  /* true: output CR */

    if ((output_flags & 0x01) != 0) {
        /* Bit 0 set: suppress newline entirely, but still may do CR */
        /* (original code: clears D0, skips LF output) */
    } else {
        if ((output_flags & 0x02) != 0) {
            /* Bit 1 set: LF only, no CR */
            do_cr = 0;
        }
        /* Output LF */
        tty_$i_put_chars(tty, newline_lf, 0x1000c);
    }

    if (do_cr < 0) {
        /* Output CR */
        tty_$i_put_chars(tty, newline_cr, 0x1000c);
    }
}

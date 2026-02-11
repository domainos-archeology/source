/*
 * TTY_$I_XMIT_CHAR - Transmit a single character
 *
 * Outputs a single character to the TTY output buffer.
 * This is a wrapper around FUN_00e1b00a (TTY output string function).
 *
 * Parameters:
 *   tty - TTY descriptor
 *   ch  - Character to transmit (in high byte of uint16_t for stack passing)
 *
 * Original address: 0x00e1b3b4
 * Size: 26 bytes
 */

#include "tty/tty_internal.h"

/* Output flags for character transmission */
#define TTY_XMIT_FLAGS  0x1000c

void TTY_$I_XMIT_CHAR(tty_desc_t *tty, uint16_t ch)
{
    /* Pass address of ch parameter - the character is in the high byte
     * due to M68K stack layout for 16-bit values */
    tty_$i_put_chars(tty, (const uint8_t *)&ch, TTY_XMIT_FLAGS);
}

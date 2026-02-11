/*
 * tty_$i_buf_insert - Insert byte into circular buffer (no lock)
 *
 * Low-level circular buffer insert without acquiring the spin lock.
 * Stores the byte at the current tail position and advances tail.
 * Wraps tail from 0x100 back to 1. Drops the byte if buffer is full.
 * Called by tty_$i_buf_put (which wraps with spin lock) and directly
 * by TTY_$I_RCV for certain character classes.
 *
 * Buffer layout:
 *   offset 0: int16_t head  (read position, 1..0x100)
 *   offset 2: int16_t tail  (write position, 1..0x100)
 *   offset 5: uint8_t data[256]  (circular data area)
 *
 * Parameters:
 *   ch  - Character to insert
 *   buf - Pointer to circular buffer header
 *
 * Original address: 0x00E1AF0A
 * Size: 56 bytes
 */

#include "tty/tty_internal.h"

void tty_$i_buf_insert(uint8_t ch, void *buf)
{
    int16_t *hdr = (int16_t *)buf;
    int16_t new_tail;

    /* Wrap tail from 0x100 back to 1 (valid range is 1..0x100) */
    if (hdr[1] == 0x100) {
        new_tail = 1;
    } else {
        new_tail = hdr[1] + 1;
    }

    /* Only insert if buffer is not full (new_tail != head) */
    if (new_tail != hdr[0]) {
        /* Store byte at current tail position + 5 byte header offset */
        *((uint8_t *)buf + hdr[1] + 5) = ch;
        hdr[1] = new_tail;
    }
}

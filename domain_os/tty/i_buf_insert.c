/*
 * tty_$i_buf_insert - Insert byte into circular buffer (no lock)
 *
 * Low-level circular buffer insert without acquiring the spin lock.
 * Stores the byte at the current tail position and advances tail.
 * Wraps tail from 0x100 back to 1. Drops the byte if buffer is full.
 * Called by tty_$i_buf_put (which wraps with spin lock) and directly
 * by TTY_$I_RCV for certain character classes.
 *
 * Buffer record (the Pascal declaration this compiles from):
 *   offset 0: int16_t head   consumer position, 1..0x100
 *   offset 2: int16_t tail   producer position, 1..0x100
 *   offset 4: int16_t size   always 0x100 (set by TTY_$I_INIT, 0xE33340)
 *   offset 6: uint8_t data[1..0x100]
 *
 * The store at 0x00E1AF32 is `move.b D0b,(0x5,A0,D2w*0x1)`, i.e.
 * buf + 5 + tail == &data[tail - 1] for the 1-based tail.  In TTY_$I_RCV the
 * same element is reached as (0x2d1,A2,tail.w) because the input record's
 * head word sits at tty + 0x2CC (see tty_desc_t in tty/tty.h).
 *
 * Parameters:
 *   ch  - Character to insert
 *   buf - Pointer to circular buffer header (&tty->input_read /
 *         &tty->output_head)
 *
 * Original address: 0x00E1AF0A
 * Size: 56 bytes
 */

#include "tty/tty_internal.h"

void tty_$i_buf_insert(uint8_t ch, void *buf)
{
    int16_t *hdr = (int16_t *)buf;
    int16_t new_tail;

    /* 0x00E1AF18: wrap tail from 0x100 back to 1 (valid range is 1..0x100) */
    if (hdr[1] == (int16_t)TTY_BUFFER_SIZE) {
        new_tail = 1;
    } else {
        new_tail = (int16_t)(hdr[1] + 1);
    }

    /* 0x00E1AF2A: only insert if the buffer is not full (new_tail != head) */
    if (new_tail != hdr[0]) {
        /* 0x00E1AF32: data[tail - 1], i.e. buf + 5 + tail */
        *((uint8_t *)buf + 5 + hdr[1]) = ch;
        hdr[1] = new_tail;
    }
}

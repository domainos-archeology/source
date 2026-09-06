/*
 * tty_$i_buf_put - Insert byte into circular buffer (with spin lock)
 *
 * (tty_$i_buf_insert, the unlocked variant, lives in i_buf_insert.c.)
 *
 * Low-level circular buffer operations for TTY output.
 *
 * Buffer layout (relative to buf pointer):
 *   offset 0: head index (uint16_t) - consumer read position
 *   offset 2: tail index (uint16_t) - producer write position
 *   offset 5: data[1..0x100] - circular buffer entries (1-based indexing)
 *
 * Valid index range: 1..0x100 (256 entries). Tail wraps from 0x100 to 1.
 * Buffer is full when next_tail == head.
 * Bytes are silently dropped when the buffer is full.
 *
 * tty_$i_buf_put (0x00e1af42, 96 bytes):
 *   Wraps the insert with ML_$SPIN_LOCK/ML_$SPIN_UNLOCK on TTY_$SPIN_LOCK.
 *   Called from tty_$i_put_chars and other output functions.
 */

#include "tty/tty_internal.h"

void tty_$i_buf_put(uint8_t ch, void *buf)
{
    uint16_t *head_ptr = (uint16_t *)buf;
    uint16_t *tail_ptr = (uint16_t *)((char *)buf + 2);
    uint8_t *data = (uint8_t *)((char *)buf + 5);

    uint16_t token = ML_$SPIN_LOCK(&TTY_$SPIN_LOCK);

    /* Compute next tail position, wrapping 0x100 -> 1 */
    uint16_t new_tail;
    if (*tail_ptr == TTY_BUFFER_SIZE) {
        new_tail = 1;
    } else {
        new_tail = *tail_ptr + 1;
    }

    /* Drop byte if buffer full (next_tail == head) */
    if (new_tail != *head_ptr) {
        data[*tail_ptr] = ch;
        *tail_ptr = new_tail;
    }

    ML_$SPIN_UNLOCK(&TTY_$SPIN_LOCK, token);
}

/*
 * tty_$i_buf_put - Put a single byte into a TTY circular buffer
 *
 * Takes the TTY spin lock, inserts the byte at the producer position,
 * advances that position (wrapping 0x100 -> 1), drops the byte when the
 * buffer is full, and releases the lock.  The insertion is the same logic
 * as tty_$i_buf_insert (0x00E1AF0A) but is inlined here between the lock
 * calls rather than calling it.  Callers: tty_$i_put_chars and its nested
 * delay helper (0x00E1AFC4..0x00E1B2C0).
 *
 * 0x00E1AF42..0x00E1AFA0 (96 bytes):
 *   0x00E1AF4A  move.b (0x8,A6),D2b            ch (byte in the high half of arg 1)
 *   0x00E1AF4E  movea.l (0xa,A6),A2            buf (arg 2)
 *   0x00E1AF52  ML_$SPIN_LOCK(&TTY_$SPIN_LOCK) -> token in (-0x6,A6)
 *   0x00E1AF64  cmpi.w #0x100,(0x2,A2)         tail == 0x100 ? 1 : tail + 1
 *   0x00E1AF76  cmp.w (A2),D0w                 new tail == head -> full, drop
 *   0x00E1AF7E  move.b D2b,(0x5,A2,D1w)        data[tail - 1]  (buf + 5 + tail)
 *   0x00E1AF82  move.w D0w,(0x2,A2)            tail = new tail
 *   0x00E1AF86  ML_$SPIN_UNLOCK(&TTY_$SPIN_LOCK, token)
 *
 * Buffer record: { head: word @0; tail: word @2; size: word @4;
 * data: array[1..0x100] of byte @6 }.
 *
 * Parameters:
 *   ch  - Character to insert
 *   buf - Pointer to the circular buffer header (&tty->output_head in the
 *         put_chars callers)
 *
 * Original address: 0x00e1af42
 * Size: 96 bytes
 */

#include "tty/tty_internal.h"

void tty_$i_buf_put(uint8_t ch, void *buf)
{
    uint16_t *head_ptr = (uint16_t *)buf;
    uint16_t *tail_ptr = (uint16_t *)((char *)buf + 2);
    uint8_t *data = (uint8_t *)((char *)buf + 5);
    ml_$spin_token_t token;
    uint16_t new_tail;

    token = ML_$SPIN_LOCK(&TTY_$SPIN_LOCK);              /* 0x00E1AF52 */

    /* 0x00E1AF64: next producer position, wrapping 0x100 -> 1 */
    if (*tail_ptr == TTY_BUFFER_SIZE) {
        new_tail = 1;
    } else {
        new_tail = (uint16_t)(*tail_ptr + 1);
    }

    /* 0x00E1AF76: drop the byte when the buffer is full */
    if (new_tail != *head_ptr) {
        data[*tail_ptr] = ch;                            /* 0x00E1AF7E */
        *tail_ptr = new_tail;                            /* 0x00E1AF82 */
    }

    ML_$SPIN_UNLOCK(&TTY_$SPIN_LOCK, token);             /* 0x00E1AF86 */
}

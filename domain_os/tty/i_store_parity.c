/*
 * TTY_$I_STORE_PARITY - Store a character with optional parity marking
 *
 * Pascal nested procedure that accesses the parent frame (TTY_$I_ERR).
 * The parent frame provides:
 *   - tty descriptor at parent_frame + 0x08
 *   - character at parent_frame + 0x0C
 *
 * If mark parity mode is enabled (input_flags bit 12), stores a
 * 3-byte sequence: 0xFF, 0x00, <char>. Otherwise stores just 0x00.
 *
 * After storing, checks the break mode to decide whether to signal
 * a reader:
 *   - If break_mode != 0 and enough characters are buffered
 *     (count >= min_chars), advances head and signals.
 *   - If break_mode == 3 and not enough chars, records the time
 *     via TIME_$CLOCK for timeout handling.
 *
 * NOTE: Because this is a Pascal nested procedure, it does not have
 * normal C parameters. In the original code it accesses the parent's
 * A6 frame directly. Here we take the parent frame values as explicit
 * parameters.
 *
 * Parameters:
 *   tty - TTY descriptor (from parent frame at offset 0x08)
 *   ch  - Character received (from parent frame at offset 0x0C)
 *
 * Original address: 0x00e1bcfc
 * Size: 268 bytes
 */

/*
 * Re-verified against 0x00E1BCFC..0x00E1BE06 (268 bytes), 2026-09-19.
 * Nested procedure of TTY_$I_ERR: A2 = (A6) is the static link and every
 * `movea.l (0x8,A2),A0` is the parent's tty, `move.b (0xc,A2)` the parent's
 * ch byte -- hence the two explicit parameters here.
 *   0x00E1BD0A  input_flags bit 12 (btst.l #0xc on the longword):
 *               buf_insert(0xFF) (st), buf_insert(0) (clr.w), buf_insert(ch)
 *   0x00E1BD48  else buf_insert(0)
 *   0x00E1BD5E  break_mode == 0 -> return
 *   0x00E1BD6A  count = input_tail - input_read (+0x100 if negative)
 *   0x00E1BD8A  count (sign-extended) < min_chars (zero-extended) ->
 *               break_mode == 3 -> TIME_$CLOCK(&(0x2c4,A0))
 *   0x00E1BD8E  else input_head = tail == 0x100 ? 1 : tail + 1;
 *               ADVANCE_EC(input_ec); state bit 4 -> SIGNAL(tty, 0x1A)
 */
#include "tty/tty_internal.h"
#include "time/time.h"

void TTY_$I_STORE_PARITY(tty_desc_t *tty, uint8_t ch)
{
    int16_t count;

    if ((tty->input_flags & 0x1000) != 0) {
        /* Mark parity mode: store 0xFF, 0x00, then the actual character */
        tty_$i_buf_insert(0xFF, &tty->input_read);
        tty_$i_buf_insert(0x00, &tty->input_read);
        tty_$i_buf_insert(ch, &tty->input_read);
    } else {
        /* No mark parity: store 0x00 as the parity error marker */
        tty_$i_buf_insert(0x00, &tty->input_read);
    }

    /* Check break mode for signaling */
    if (tty->break_mode != 0) {
        /* Calculate buffered character count */
        count = tty->input_tail - tty->input_read;
        if (count < 0) {
            count = count + 0x100;
        }

        if ((int32_t)count < (int32_t)(uint32_t)tty->min_chars) {
            /* Not enough characters yet */
            if (tty->break_mode == 3) {
                /* Mode 3: record time for timeout handling */
                TIME_$CLOCK((clock_t *)&tty->last_input_clock_high);
            }
        } else {
            /* Enough characters: advance head pointer */
            if (tty->input_tail == 0x100) {
                tty->input_head = 1;
            } else {
                tty->input_head = tty->input_tail + 1;
            }

            /* Signal reader via input eventcount */
            TTY_$I_ADVANCE_EC(tty->input_ec);

            /* If signal pending, send SIGWINCH */
            if ((tty->state_flags & 0x10) != 0) {
                TTY_$I_SIGNAL(tty, TTY_SIG_WINCH);
            }
        }
    }
}

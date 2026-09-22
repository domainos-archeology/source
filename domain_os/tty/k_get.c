/*
 * TTY_$K_GET - Read characters from TTY (kernel level)
 *
 * Reads characters from the TTY input buffer. Handles both blocking
 * and non-blocking reads, line discipline processing, and special
 * character handling.
 *
 * Parameters:
 *   line_ptr   - Pointer to terminal line number
 *   options    - Pointer to read options (2-byte flags)
 *                  byte 1 bit 0: wait for data if buffer empty
 *                  byte 1 bit 1: clear buffer after read
 *   buffer     - Buffer to receive characters
 *   count      - Pointer to max count (updated with actual count)
 *   status_ret - Pointer to receive status code
 *
 * Returns:
 *   Number of characters read
 *
 * Original address: 0x00e1c3d2
 * Size: 590 bytes
 */

/*
 * TTY_$K_GET - re-verified against 0x00E1C3D2..0x00E1C61E (590 bytes),
 * 2026-09-22.  A5 = 0x00E2DDB4; A4 = options, A3 = count ptr, D3 = chars
 * read (the result, D0w at 0x00E1C614), (-0x4,A6) = output cursor,
 * (-0x18) = wait flag (options bit 0), D4 = peek flag (options bit 1: keep
 * the ring bytes and do not move input_read), (-0x1e) = done, (-0x1c) = the
 * flag tty_$i_wait sets, D5 = seq(break_mode == 0) i.e. canonical mode.
 *   0x00E1C3E8  GET_DESC(*line_ptr, status); status != 0 -> return 0
 *   0x00E1C40C  TTY_$I_LOCK(tty)
 *   0x00E1C42E  loop: D5 recomputed each pass; cursor = buffer + D3
 *   0x00E1C444  canonical: input_read == input_head -> nothing to copy;
 *               else while pos != input_tail and D3 < *count (bcs):
 *                 ch = ring[pos-1]; !peek -> ring[pos-1] = 0; pos++ (wrap);
 *                 class 0x0C: D3 == 0 -> status 0x350005; done; stop
 *                 class 0x03: TTY_$I_SIGNAL(tty, 0x15); stop (done unset)
 *                 else store, D3++; class 0x0E or 0x0B -> done; stop
 *   0x00E1C4FC  non-canonical: while pos != input_tail and D3 < *count:
 *                 store, !peek -> clear, D3++, pos++; then done = (D3 == *count)
 *   0x00E1C50E  !peek: n = input_tail - input_read (+0x100);
 *               (ext n - zext D3) < 0x40 and n >= 0x40 and flow handler ->
 *               spin lock; handler(line_id, false, input_flags bit 1); unlock;
 *               input_read = pos
 *   0x00E1C57C  done -> finish; wait-flag byte set -> done;
 *               D3 >= *count -> status 0x350004; canonical or D3 < min_chars
 *               -> tty_$i_wait(tty, wait, &flag, D3, status); else done
 *   0x00E1C5C0  status != 0 -> done; loop while !done
 *   0x00E1C5D4  pending_signal != 0: bit 0 -> status = status_handler(line_id,
 *               true); else bit 1 -> status 0x350009; pending_signal = 0
 *   0x00E1C60E  TTY_$I_UNLOCK(tty); return D3
 */
#include "tty/tty_internal.h"
#include "proc1/proc1.h"
#include "fim/fim.h"
#include "ec/ec.h"

/* Status codes (status_$tty_buffer_full / status_$tty_eof come from base/base.h) */

/* tty_$i_wait declared in tty/tty_internal.h */

ushort TTY_$K_GET(short *line_ptr, void *options, void *buffer,
                  ushort *count, status_$t *status_ret)
{
    tty_desc_t *tty;
    uint16_t chars_read = 0;
    uint8_t *buf_ptr;
    int16_t read_pos;
    int16_t break_mode_flag;
    char wait_flag;
    char clear_flag;
    char done;
    char eof_flag;
    uint8_t ch;
    uint16_t char_class;
    ml_$spin_token_t token;
    int16_t buffer_count;

    /* Get TTY descriptor for this line */
    tty = TTY_$I_GET_DESC(*line_ptr, status_ret);
    if (*status_ret != status_$ok) {
        return 0;
    }

    /* Lock the TTY */
    TTY_$I_LOCK(tty);

    /* Extract option flags */
    /* Original: btst.b #0,(0x1,A4) / btst.b #1,(0x1,A4) - bits 0 and 1 of the
     * low byte of the 2-byte big-endian option word. */
    wait_flag = -((*(uint16_t *)options & 0x0001) != 0);
    clear_flag = -((*(uint16_t *)options & 0x0002) != 0);
    done = 0;
    eof_flag = 0;

    /* Determine if we're in break mode (line-oriented) */
    break_mode_flag = -(tty->break_mode == 0);

    do {
        buf_ptr = (uint8_t *)buffer + chars_read;
        read_pos = tty->input_read;

        if (break_mode_flag < 0) {
            /* Raw mode - no break character processing */
            if (read_pos != tty->input_head) {
                /* Read characters until buffer empty or count reached */
                while (read_pos != tty->input_tail && chars_read < *count) {
                    /* Get character from buffer */
                    ch = tty->input_buffer[read_pos - 1];

                    /* Clear buffer position if requested */
                    if (clear_flag >= 0) {
                        tty->input_buffer[read_pos - 1] = 0;
                    }

                    /* Advance read position (circular buffer 1-256) */
                    if (read_pos == 0x100) {
                        read_pos = 1;
                    } else {
                        read_pos++;
                    }

                    /* Get character class */
                    char_class = tty->char_class[ch];

                    /* Handle special characters */
                    if (char_class == TTY_CHAR_CLASS_DISCARD) {  /* 0x0C */
                        /* EOF indicator */
                        if (chars_read == 0) {
                            *status_ret = status_$tty_eof;
                        }
                        done = -1;
                        goto update_read_pos;
                    }

                    if (char_class == TTY_CHAR_CLASS_BREAK) {  /* 0x03 */
                        /* Break/suspend - send TSTP signal */
                        TTY_$I_SIGNAL(tty, TTY_SIG_TSTP);
                        goto update_read_pos;
                    }

                    /* Store character in output buffer */
                    *buf_ptr++ = ch;
                    chars_read++;

                    /* Check for line terminators (CR or NL) */
                    if (char_class == TTY_CHAR_CLASS_CR ||
                        char_class == TTY_CHAR_CLASS_NL) {
                        done = -1;
                        goto update_read_pos;
                    }
                }
            }
        } else {
            /* Line mode - read until break or buffer full */
            while (read_pos != tty->input_tail && chars_read < *count) {
                /* Get character from buffer */
                *buf_ptr = tty->input_buffer[read_pos - 1];

                /* Clear buffer position if requested */
                if (clear_flag >= 0) {
                    tty->input_buffer[read_pos - 1] = 0;
                }

                buf_ptr++;
                chars_read++;

                /* Advance read position (circular buffer 1-256) */
                if (read_pos == 0x100) {
                    read_pos = 1;
                } else {
                    read_pos++;
                }
            }

            /* Mark done if we read the requested count */
            done = -(chars_read == *count);
        }

update_read_pos:
        /* Update read position if clearing */
        if (clear_flag >= 0) {
            /* Calculate remaining buffer count */
            buffer_count = tty->input_tail - tty->input_read;
            if (buffer_count < 0) {
                buffer_count += 0x100;
            }

            /* Check if flow control callback needed */
            if ((buffer_count - chars_read) < 0x40 &&
                buffer_count >= 0x40 &&
                tty->flow_ctrl_handler != 0) {
                token = ML_$SPIN_LOCK(&TTY_$SPIN_LOCK);
                /* 0xE1C54E: sne on btst.b #1,(0x17,A2) == bit 1 of the long */
                boolean hw_flow = (tty->input_flags & 0x02) != 0 ? true : false;
                tty->flow_ctrl_handler(tty->line_id, false, hw_flow);
                ML_$SPIN_UNLOCK(&TTY_$SPIN_LOCK, token);
            }

            tty->input_read = read_pos;
        }

        /* Check if done */
        if (done < 0) {
            break;
        }

        /* Check for EOF flag set */
        if (eof_flag < 0) {
            done = -1;
        } else if (chars_read < *count) {
            /* More characters requested */
            if (break_mode_flag >= 0 && chars_read >= tty->min_chars) {
                /* Minimum character count satisfied in line mode */
                done = -1;
            } else {
                /* Wait for more data */
                tty_$i_wait(tty, wait_flag, &eof_flag, chars_read, status_ret);
            }
        } else {
            /* Buffer full */
            *status_ret = status_$tty_buffer_full;
        }

        /* Check for error status */
        if (*status_ret != status_$ok) {
            done = -1;
        }

    } while (done >= 0);

    /* Handle any pending signal */
    if (tty->pending_signal != 0) {
        /* btst.b #0,(0xb,A2) = bit 0 of the 16-bit pending_signal (the old
         * byte-pointer cast read the wrong byte on a little-endian host) */
        if ((tty->pending_signal & TTY_ERR_CALLBACK) != 0) {
            /* 0x00E1C5E2: subq/st/move.l (A2)/jsr (0x2c0,A2): the full
             * 32-bit line_id and a true boolean, result longword in D0 */
            *status_ret = tty->status_handler(tty->line_id, true);
        } else if ((tty->pending_signal & TTY_ERR_OVERFLOW) != 0) {
            *status_ret = status_$tty_input_buffer_overrun;
        }
        tty->pending_signal = 0;
    }

    /* Unlock TTY */
    TTY_$I_UNLOCK(tty);

    return chars_read;
}

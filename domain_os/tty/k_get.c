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

#include "tty/tty_internal.h"
#include "proc1/proc1.h"
#include "fim/fim.h"
#include "ec/ec.h"

/* Status codes */
#define status_$tty_buffer_full    0x350004
#define status_$tty_eof            0x350005
#define status_$tty_quit           0x350007
#define status_$tty_overflow       0x350009

/* External helper functions */
extern void FUN_00e1c204(tty_desc_t *tty, char wait_flag, char *done_flag,
                         uint16_t count, status_$t *status);

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
    uint16_t token;
    int16_t buffer_count;

    /* Get TTY descriptor for this line */
    tty = TTY_$I_GET_DESC(*line_ptr, status_ret);
    if (*status_ret != status_$ok) {
        return 0;
    }

    /* Lock the TTY */
    TTY_$I_LOCK(tty);

    /* Extract option flags */
    wait_flag = -((*(uint8_t *)((char *)options + 1) & 0x01) != 0);
    clear_flag = -((*(uint8_t *)((char *)options + 1) & 0x02) != 0);
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
                    ch = tty->input_buffer[read_pos];

                    /* Clear buffer position if requested */
                    if (clear_flag >= 0) {
                        tty->input_buffer[read_pos] = 0;
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
                *buf_ptr = tty->input_buffer[read_pos];

                /* Clear buffer position if requested */
                if (clear_flag >= 0) {
                    tty->input_buffer[read_pos] = 0;
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
                int8_t hw_flow = -((*(uint8_t *)((char *)tty + 0x17) & 0x02) != 0);
                ((void (*)(short, int8_t, int8_t))tty->flow_ctrl_handler)(
                    (short)tty->line_id, 0, hw_flow);
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
                FUN_00e1c204(tty, wait_flag, &eof_flag, chars_read, status_ret);
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
        if ((*(uint8_t *)((char *)tty + 0x0b) & TTY_ERR_CALLBACK) != 0) {
            /* Call error handler */
            *status_ret = ((status_$t (*)(short))tty->status_handler)(
                (short)tty->line_id);
        } else if ((*(uint8_t *)((char *)tty + 0x0b) & TTY_ERR_OVERFLOW) != 0) {
            *status_ret = status_$tty_overflow;
        }
        tty->pending_signal = 0;
    }

    /* Unlock TTY */
    TTY_$I_UNLOCK(tty);

    return chars_read;
}

/*
 * TTY_$I_RCV - Receive a character (interrupt level)
 *
 * Processes incoming characters for a TTY, handling special control
 * characters based on the character class table. This is the core
 * input processing routine called at interrupt level.
 *
 * Parameters:
 *   tty - TTY descriptor
 *   ch  - Character received
 *
 * Original address: 0x00e1b92a
 * Size: 936 bytes
 */

#include "tty/tty_internal.h"
#include "time/time.h"
#include "mmu/mmu.h"
#include "misc/crash_system.h"

/* Internal helper function prototypes */
static void tty_echo_char(tty_desc_t *tty, uint8_t ch);
static void tty_output_char(tty_desc_t *tty, uint16_t ch);
static void tty_store_input(tty_desc_t *tty, uint8_t ch);
static void tty_delete_char(tty_desc_t *tty);
static void tty_word_erase(tty_desc_t *tty);
static void tty_kill_line(tty_desc_t *tty);
static void tty_reprint_line(tty_desc_t *tty);

/* External helper functions from other TTY modules */
extern void FUN_00e1af0a(uint8_t ch, void *ptr);
/* TTY_$I_ECHO_CHAR is declared in tty_internal.h */
/* TTY_$I_XMIT_CHAR is declared in tty_internal.h */
extern void FUN_00e1b8b0(tty_desc_t *tty, uint8_t ch);
extern void FUN_00e1b538(tty_desc_t *tty);
extern void FUN_00e1b6ac(tty_desc_t *tty);
extern void FUN_00e1b716(tty_desc_t *tty);
extern void FUN_00e1b456(tty_desc_t *tty);

/* Error status for crash */
extern status_$t status_$t_00e1bcf8;

void TTY_$I_RCV(tty_desc_t *tty, uint8_t ch)
{
    uint16_t state_flags;
    uint16_t char_class;
    int16_t next_tail;
    int16_t buffer_count;

    /* Calculate next tail position (circular buffer 1-256) */
    if (tty->input_tail == 0x100) {
        next_tail = 1;
    } else {
        next_tail = tty->input_tail + 1;
    }

    /* Check for buffer overflow */
    if (next_tail == tty->input_read) {
        /* Set overflow flag */
        *(uint8_t *)((char *)tty + 0x0b) |= TTY_ERR_OVERFLOW;
    }

    /* Handle parity stripping */
    if ((tty->input_flags & 0x2000) != 0) {
        /* Strip high bit for 7-bit mode */
        ch &= 0x7f;
    } else {
        /* Check for 0xFF with mark parity enabled */
        if (ch == 0xff && (*(uint16_t *)((char *)tty + 0x16) & 0x1000) != 0) {
            FUN_00e1af0a(0xff, &tty->input_read);
        }
    }

    /* Save and mask state flags */
    state_flags = tty->state_flags;
    tty->state_flags &= 0xff1f;  /* Clear processing bits */

    /* Get character class - raw mode uses class 0x12 (normal) */
    if ((state_flags & TTY_FLAG_RAW_MODE) != 0) {
        char_class = TTY_CHAR_CLASS_NORMAL;
    } else {
        char_class = tty->char_class[ch];
    }

    /* Process character based on class */
    switch (char_class) {
        case TTY_CHAR_CLASS_SIGINT:  /* 0x00 - Interrupt */
            TTY_$I_ECHO_CHAR(tty, ch);
            TTY_$I_FLUSH_INPUT(tty);
            TTY_$I_SIGNAL(tty, TTY_SIG_INT);
            break;

        case TTY_CHAR_CLASS_SIGQUIT:  /* 0x01 - Quit */
            TTY_$I_ECHO_CHAR(tty, ch);
            TTY_$I_FLUSH_INPUT(tty);
            TTY_$I_SIGNAL(tty, TTY_SIG_QUIT);
            break;

        case TTY_CHAR_CLASS_SIGTSTP:  /* 0x02 - Suspend */
            TTY_$I_ECHO_CHAR(tty, ch);
            TTY_$I_FLUSH_INPUT(tty);
            TTY_$I_SIGNAL(tty, TTY_SIG_TSTP);
            break;

        case TTY_CHAR_CLASS_BREAK:  /* 0x03 - Break */
        case TTY_CHAR_CLASS_NL:     /* 0x0B - Newline */
            FUN_00e1b8b0(tty, ch);
            break;

        case TTY_CHAR_CLASS_EOF:  /* 0x04 - End of file */
            /* Set EOF pending flag */
            *(uint8_t *)((char *)tty + 0x09) |= TTY_STATUS_EOF_PEND;
            if ((*(uint8_t *)((char *)tty + 0x17) & 0x01) != 0) {
                TTY_$I_ECHO_CHAR(tty, ch);
                TTY_$I_XMIT_CHAR(tty, 0x0800);
            }
            break;

        case TTY_CHAR_CLASS_XON:  /* 0x05 - Resume output */
            /* Set XON/XOFF flag */
            *(uint8_t *)((char *)tty + 0x09) |= TTY_STATUS_XON_XOFF;
            if (tty->xon_xoff_handler != 0) {
                ((void (*)(short))tty->xon_xoff_handler)((short)tty->line_id);
            }
            break;

        case TTY_CHAR_CLASS_XOFF:  /* 0x06 - Stop output */
            /* Clear XON/XOFF flag */
            *(uint8_t *)((char *)tty + 0x09) &= ~TTY_STATUS_XON_XOFF;
            if (tty->xon_xoff_handler != 0) {
                ((void (*)(short))tty->xon_xoff_handler)((short)tty->line_id);
            }
            TTY_$I_ADVANCE_EC(tty->output_ec);
            break;

        case TTY_CHAR_CLASS_DEL:  /* 0x07 - Delete character */
            tty->state_flags = (state_flags & 0x80) | tty->state_flags;
            FUN_00e1b538(tty);
            break;

        case TTY_CHAR_CLASS_WERASE:  /* 0x08 - Word erase */
            tty->state_flags = (state_flags & 0x80) | tty->state_flags;
            FUN_00e1b6ac(tty);
            break;

        case TTY_CHAR_CLASS_KILL:  /* 0x09 - Kill line */
            tty->state_flags = (state_flags & 0x80) | tty->state_flags;
            FUN_00e1b716(tty);
            break;

        case TTY_CHAR_CLASS_REPRINT:  /* 0x0A - Reprint line */
            if ((*(uint8_t *)((char *)tty + 0x17) & 0x01) != 0) {
                int16_t pos;
                TTY_$I_ECHO_CHAR(tty, ch);
                FUN_00e1b456(tty);
                pos = tty->input_head;
                while (pos != tty->input_tail) {
                    TTY_$I_ECHO_CHAR(tty, tty->input_buffer[pos]);
                    if (pos == 0x100) {
                        pos = 1;
                    } else {
                        pos++;
                    }
                }
            }
            break;

        case TTY_CHAR_CLASS_DISCARD:  /* 0x0C - Discard output */
            TTY_$I_ECHO_CHAR(tty, ch);
            TTY_$I_XMIT_CHAR(tty, 0x0800);
            TTY_$I_XMIT_CHAR(tty, 0x0800);
            FUN_00e1b8b0(tty, ch);
            break;

        case TTY_CHAR_CLASS_FLUSHOUT:  /* 0x0D - Flush output */
            if ((state_flags & TTY_STATUS_OUTPUT_FLUSH) == 0) {
                TTY_$I_FLUSH_OUTPUT(tty);
                *(uint8_t *)((char *)tty + 0x09) |= TTY_STATUS_OUTPUT_FLUSH;
            } else if ((*(uint8_t *)((char *)tty + 0x17) & 0x01) != 0) {
                TTY_$I_ECHO_CHAR(tty, ch);
            }
            break;

        case TTY_CHAR_CLASS_CR:  /* 0x0E - Carriage return */
            if ((*(uint8_t *)((char *)tty + 0x17) & 0x10) != 0) {
                if ((*(uint8_t *)((char *)tty + 0x17) & 0x08) != 0) {
                    return;
                }
                /* Translate CR to NL */
                TTY_$I_RCV(tty, 0x0d);
                return;
            }
            FUN_00e1b8b0(tty, ch);
            break;

        case TTY_CHAR_CLASS_CRLF:  /* 0x0F - CR/LF handling */
            {
                uint16_t flags = *(uint16_t *)((char *)tty + 0x16);
                if ((flags & 0x200) != 0) {
                    return;
                }
                if ((flags & 0x08) != 0) {
                    if ((flags & 0x10) != 0) {
                        return;
                    }
                    /* Translate to newline */
                    TTY_$I_RCV(tty, 0x0a);
                    return;
                }
            }
            /* Fall through to normal processing */
            /* FALLTHROUGH */

        case TTY_CHAR_CLASS_NORMAL:  /* 0x12 - Normal character */
            /* Check for buffer overflow */
            if ((*(uint8_t *)((char *)tty + 0x0b) & TTY_ERR_OVERFLOW) == 0) {
                /* Mark start of new line if buffer empty */
                if (tty->input_tail == tty->input_head) {
                    *(uint16_t *)((char *)tty + 0x56) = *(uint16_t *)((char *)tty + 0x58);
                }
                /* Store character in buffer */
                tty->input_buffer[tty->input_tail] = ch;
                tty->input_tail = next_tail;

                /* Check buffer high water mark for flow control */
                buffer_count = tty->input_tail - tty->input_read;
                if (buffer_count < 0) {
                    buffer_count += 0x100;
                }
                if (buffer_count > 0xbf && tty->flow_ctrl_handler != 0) {
                    int8_t hw_flow = -((*(uint8_t *)((char *)tty + 0x17) & 0x02) != 0);
                    ((void (*)(short, int8_t, int8_t))tty->flow_ctrl_handler)(
                        (short)tty->line_id, -1, hw_flow);
                }

                /* Echo if parity error flag set */
                if ((int8_t)state_flags < 0) {
                    TTY_$I_XMIT_CHAR(tty, 0x2f00);
                }

                /* Echo character if echo enabled */
                if ((*(uint8_t *)((char *)tty + 0x17) & 0x01) != 0) {
                    TTY_$I_ECHO_CHAR(tty, ch);
                }
            }

            /* Handle break mode signaling */
            if (tty->break_mode != 0) {
                tty->input_head = next_tail;
                TTY_$I_ADVANCE_EC(tty->input_ec);

                /* Send signal if pending */
                if ((*(uint8_t *)((char *)tty + 0x09) & TTY_STATUS_SIG_PEND) != 0) {
                    TTY_$I_SIGNAL(tty, TTY_SIG_WINCH);
                }

                /* Update clock if break mode 3 */
                if (tty->break_mode == 3) {
                    TIME_$CLOCK((clock_t *)&tty->reserved_2C4);
                }
            }
            break;

        case TTY_CHAR_CLASS_TAB:  /* 0x10 - Tab character */
            /* Mark start of new line if buffer empty */
            if (tty->input_tail == tty->input_head) {
                *(uint16_t *)((char *)tty + 0x56) = *(uint16_t *)((char *)tty + 0x58);
            }
            FUN_00e1af0a(ch, &tty->input_read);
            if ((*(uint8_t *)((char *)tty + 0x17) & 0x01) != 0) {
                TTY_$I_XMIT_CHAR(tty, (ch << 8) | (uint8_t)(uintptr_t)&tty->input_read);
            }
            break;

        case TTY_CHAR_CLASS_CRASH:  /* 0x11 - Crash system */
            {
                char normal_mode = MMU_$NORMAL_MODE();
                if (normal_mode >= 0) {
                    CRASH_SYSTEM(&status_$t_00e1bcf8);
                }
            }
            break;

        default:
            break;
    }
}

/*
 * TTY_$I_RCV - Receive a character (interrupt level)
 *
 * Processes an incoming character for a TTY line.  The character is looked up
 * in the per-line character-class table (tty->char_class[]) and dispatched
 * through a 19-entry jump table; raw mode short-circuits the lookup and uses
 * class 0x12 (normal) for every character.
 *
 * Parameters (Pascal calling sequence, caller cleans up):
 *   (0x08,A6) tty - TTY descriptor (pushed with pea)
 *   (0x0C,A6) ch  - Character, in the HIGH byte of a 2-byte stack slot
 *                   (`move.b Dn,-(SP)` / `move.w #0xNN00,-(SP)`)
 *   (0x0E,A6)     - 2-byte Pascal result slot, unused (this is a procedure)
 *
 * A5 is loaded with the TTY module data base (0x00E2DDB4) at 0x00E1B932 but
 * this function makes no (d,A5) reference; the load is dead here and is left
 * out of the C.
 *
 * Original address: 0x00e1b92a
 * Size: 936 bytes
 * Jump table: 0x00e1b9c0, 19 word-sized self-relative entries.
 */

#include "tty/tty_internal.h"
#include "time/time.h"
#include "mmu/mmu.h"
#include "misc/crash_system.h"

/*
 * Constant status cell passed by reference at 0x00E1BA92:
 *   pea (0x264,PC)  ->  0x00E1BA94 + 0x264 = 0x00E1BCF8
 * The cell at 0x00E1BCF8 holds 0x000B0008.
 */
static const status_$t crash_status_00e1bcf8 = 0x000B0008;

void TTY_$I_RCV(tty_desc_t *tty, uint8_t ch)
{
    uint16_t state_flags;   /* D4w: state_flags as it was on entry */
    uint16_t char_class;    /* D0w */
    int16_t next_tail;      /* D2w */
    int16_t buffer_count;   /* D0w */

    /* 0xE1B940: next write position, wrapping 0x100 -> 1 (1-based ring) */
    if (tty->input_tail == TTY_BUFFER_SIZE) {
        next_tail = 1;
    } else {
        next_tail = (int16_t)(tty->input_tail + 1);
    }

    /* 0xE1B952: ring full -> set the overflow bit (bset.b #1,(0xb,A2)) */
    if (next_tail == (int16_t)tty->input_read) {
        tty->pending_signal |= TTY_ERR_OVERFLOW;
    }

    /* 0xE1B95E: btst.b #5,(0x17,A2) == bit 5 of the long at 0x14 */
    if ((tty->input_flags & 0x00000020) != 0) {
        /* 0xE1B966: 7-bit line, strip the high bit */
        ch &= 0x7f;
    } else if (ch == 0xff && (tty->input_flags & 0x00001000) != 0) {
        /* 0xE1B96C: 0xFF is the parity-escape prefix; stuff it literally */
        tty_$i_buf_insert(0xff, &tty->input_read);
    }

    /* 0xE1B98A: snapshot state_flags, then clear bits 5..7 (andi.w #0xFF1F) */
    state_flags = tty->state_flags;
    tty->state_flags &= 0xff1f;

    /* 0xE1B994: btst.l #6,D4 - raw mode forces class NORMAL */
    if ((state_flags & TTY_FLAG_RAW_MODE) != 0) {
        char_class = TTY_CHAR_CLASS_NORMAL;
    } else {
        char_class = tty->char_class[ch];
    }

    /* 0xE1B9AE: cmpi.w #0x13 / bcc -> unsigned out of range falls out */
    switch (char_class) {
        case TTY_CHAR_CLASS_SIGINT:  /* 0x00 - 0xE1B9E6 */
            TTY_$I_ECHO_CHAR(tty, ch);
            TTY_$I_FLUSH_INPUT(tty);
            TTY_$I_SIGNAL(tty, TTY_SIG_INT);
            break;

        case TTY_CHAR_CLASS_SIGQUIT:  /* 0x01 - 0xE1BA02 */
            TTY_$I_ECHO_CHAR(tty, ch);
            TTY_$I_FLUSH_INPUT(tty);
            TTY_$I_SIGNAL(tty, TTY_SIG_QUIT);
            break;

        case TTY_CHAR_CLASS_SIGTSTP:  /* 0x02 - 0xE1BA1E */
            TTY_$I_ECHO_CHAR(tty, ch);
            TTY_$I_FLUSH_INPUT(tty);
            TTY_$I_SIGNAL(tty, TTY_SIG_TSTP);
            break;

        case TTY_CHAR_CLASS_BREAK:  /* 0x03 - 0xE1BB32 */
        case TTY_CHAR_CLASS_NL:     /* 0x0B - 0xE1BB32 (same target) */
            TTY_$I_BREAK_CHAR(tty, ch);
            break;

        case TTY_CHAR_CLASS_EOF:  /* 0x04 - 0xE1BB40 */
            tty->state_flags |= TTY_STATUS_EOF_PEND;   /* bset.b #6,(0x9,A2) */
            if ((tty->input_flags & 0x00000001) != 0) {
                TTY_$I_ECHO_CHAR(tty, ch);
                /*
                 * 0xE1BB5C and 0xE1BB6A: TTY_$I_XMIT_CHAR is called TWICE with
                 * 0x0800 (character 0x08, backspace).  The second call falls
                 * through into the shared tail at 0xE1BB70.
                 */
                TTY_$I_XMIT_CHAR(tty, 0x0800);
                TTY_$I_XMIT_CHAR(tty, 0x0800);
            }
            break;

        case TTY_CHAR_CLASS_XON:  /* 0x05 - 0xE1BA42: stop output */
            tty->state_flags |= TTY_STATUS_XON_XOFF;   /* bset.b #2,(0x9,A2) */
            if (tty->xon_xoff_handler != 0) {
                /* 0xE1BA52: st -(SP) pushes the boolean TRUE (0xFF) */
                tty->xon_xoff_handler(tty->line_id, true);
            }
            break;

        case TTY_CHAR_CLASS_XOFF:  /* 0x06 - 0xE1BA60: resume output */
            tty->state_flags &= (uint16_t)~TTY_STATUS_XON_XOFF; /* bclr.b #2 */
            if (tty->xon_xoff_handler != 0) {
                /* 0xE1BA6E: clr.w -(SP) pushes the boolean FALSE */
                tty->xon_xoff_handler(tty->line_id, false);
            }
            TTY_$I_ADVANCE_EC(tty->output_ec);
            break;

        case TTY_CHAR_CLASS_DEL:  /* 0x07 - 0xE1BACE */
            tty->state_flags |= (uint16_t)(state_flags & TTY_FLAG_PARITY_ERR);
            TTY_$I_DELETE_CHAR(tty);
            break;

        case TTY_CHAR_CLASS_KILL:  /* 0x08 - 0xE1BAF6 -> 0xE1B6AC */
            tty->state_flags |= (uint16_t)(state_flags & TTY_FLAG_PARITY_ERR);
            TTY_$I_KILL_LINE(tty);
            break;

        case TTY_CHAR_CLASS_WERASE:  /* 0x09 - 0xE1BAE2 -> 0xE1B716 */
            tty->state_flags |= (uint16_t)(state_flags & TTY_FLAG_PARITY_ERR);
            TTY_$I_WORD_ERASE(tty);
            break;

        case TTY_CHAR_CLASS_REPRINT:  /* 0x0A - 0xE1BBEC */
            if ((tty->input_flags & 0x00000001) != 0) {
                int16_t pos;
                TTY_$I_ECHO_CHAR(tty, ch);
                TTY_$I_NEWLINE(tty);
                /* 0xE1BC0A: re-echo input_head .. input_tail */
                pos = (int16_t)tty->input_head;
                while (pos != (int16_t)tty->input_tail) {
                    TTY_$I_ECHO_CHAR(tty, tty->input_buffer[pos - 1]);
                    if (pos == TTY_BUFFER_SIZE) {
                        pos = 1;
                    } else {
                        pos = (int16_t)(pos + 1);
                    }
                }
            }
            break;

        case TTY_CHAR_CLASS_DISCARD:  /* 0x0C - 0xE1BB0A */
            TTY_$I_ECHO_CHAR(tty, ch);
            TTY_$I_XMIT_CHAR(tty, 0x0800);
            TTY_$I_XMIT_CHAR(tty, 0x0800);
            /* falls into the case 3 tail at 0xE1BB32 */
            TTY_$I_BREAK_CHAR(tty, ch);
            break;

        case TTY_CHAR_CLASS_FLUSHOUT:  /* 0x0D - 0xE1BAA0 */
            if ((state_flags & TTY_STATUS_OUTPUT_FLUSH) == 0) {
                TTY_$I_FLUSH_OUTPUT(tty);
                tty->state_flags |= TTY_STATUS_OUTPUT_FLUSH; /* bset.b #5 */
            } else if ((tty->input_flags & 0x00000001) != 0) {
                TTY_$I_ECHO_CHAR(tty, ch);
            }
            break;

        case TTY_CHAR_CLASS_CR:  /* 0x0E - 0xE1BBA8 */
            if ((tty->input_flags & 0x00000010) != 0) {
                if ((tty->input_flags & 0x00000008) != 0) {
                    return;
                }
                /* 0xE1BBBC: re-dispatch as 0x0D */
                TTY_$I_RCV(tty, 0x0d);
                return;
            }
            /* 0xE1BBAE: branches to the case-3 tail */
            TTY_$I_BREAK_CHAR(tty, ch);
            break;

        case TTY_CHAR_CLASS_CRLF:  /* 0x0F - 0xE1BBC2 */
            if ((tty->input_flags & 0x00000200) != 0) {
                return;
            }
            if ((tty->input_flags & 0x00000008) != 0) {
                if ((tty->input_flags & 0x00000010) != 0) {
                    return;
                }
                /* 0xE1BBDE: re-dispatch as 0x0A */
                TTY_$I_RCV(tty, 0x0a);
                return;
            }
            /* 0xE1BBD2: beq falls into the class-0x12 body at 0xE1BC38 */
            /* FALLTHROUGH */

        case TTY_CHAR_CLASS_NORMAL:  /* 0x12 - 0xE1BC38 */
            /* 0xE1BC38: skip the store when the ring already overflowed */
            if ((tty->pending_signal & TTY_ERR_OVERFLOW) == 0) {
                if (tty->input_tail == tty->input_head) {
                    /* 0xE1BC4A: first char of a line - latch the column */
                    tty->saved_input_flags = tty->column;
                }
                /* 0xE1BC54: move.b D3b,(0x2d1,A0) with A0 = A2 + input_tail */
                tty->input_buffer[tty->input_tail - 1] = ch;
                tty->input_tail = (uint16_t)next_tail;

                /* 0xE1BC5C: characters pending for the reader */
                buffer_count = (int16_t)(tty->input_tail - tty->input_read);
                if (buffer_count < 0) {
                    buffer_count = (int16_t)(buffer_count + 0x100);
                }
                if (buffer_count >= 0xc0 && tty->flow_ctrl_handler != 0) {
                    /* 0xE1BC76: sne on btst.b #1,(0x17,A2) -> 0xFF / 0x00 */
                    boolean use_hw_flow =
                        ((tty->input_flags & 0x00000002) != 0) ? true : false;
                    tty->flow_ctrl_handler(tty->line_id, true, use_hw_flow);
                }

                /* 0xE1BC8C: tst.b D4b - bit 7 of the saved state_flags */
                if ((int8_t)(state_flags & 0xff) < 0) {
                    TTY_$I_XMIT_CHAR(tty, 0x2f00);
                }

                if ((tty->input_flags & 0x00000001) != 0) {
                    TTY_$I_ECHO_CHAR(tty, ch);
                }
            }

            /* 0xE1BCB2: in a break mode, every character completes a "line" */
            if (tty->break_mode != 0) {
                tty->input_head = (uint16_t)next_tail;
                TTY_$I_ADVANCE_EC(tty->input_ec);

                if ((tty->state_flags & TTY_STATUS_SIG_PEND) != 0) {
                    TTY_$I_SIGNAL(tty, TTY_SIG_WINCH);
                }

                if (tty->break_mode == 3) {
                    /* 0xE1BCE4: pea (0x2c4,A2) - the 48-bit clock cell */
                    TIME_$CLOCK((clock_t *)&tty->last_input_clock_high);
                }
            }
            break;

        case TTY_CHAR_CLASS_TAB:  /* 0x10 - 0xE1BB7A */
            if (tty->input_tail == tty->input_head) {
                tty->saved_input_flags = tty->column;
            }
            tty_$i_buf_insert(ch, &tty->input_read);
            if ((tty->input_flags & 0x00000001) != 0) {
                /*
                 * 0xE1BBA4: `move.b D3b,-(SP)` then branch into the shared
                 * TTY_$I_XMIT_CHAR tail, so the character occupies the high
                 * byte of the argument word and the low byte is whatever the
                 * stack held.  Only the high byte is read by the callee.
                 */
                TTY_$I_XMIT_CHAR(tty, (uint16_t)(ch << 8));
            }
            break;

        case TTY_CHAR_CLASS_CRASH:  /* 0x11 - 0xE1BA86 */
            {
                /* tst.b D0b / bmi -> crash only when NOT in normal mode */
                int8_t normal_mode = MMU_$NORMAL_MODE();
                if (normal_mode >= 0) {
                    CRASH_SYSTEM(&crash_status_00e1bcf8);
                }
            }
            break;

        default:
            /* 0xE1B9B2: class >= 0x13 falls straight through to the exit */
            break;
    }
}

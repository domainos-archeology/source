/*
 * TTY_$I_BREAK_CHAR - Process a break/newline character
 *
 * Handles characters that terminate a line of input: stores the character
 * in the input buffer, echoes it if input echo is enabled, commits the line
 * (head := tail), latches the column, and advances the input eventcount to
 * wake any reader.  Only caller: TTY_$I_RCV (0x00E1BB38).
 *
 * 0x00E1B8B0..0x00E1B928 (122 bytes), A2 = tty:
 *   0x00E1B8BA  subq/pea (0x2cc,A2)/move.b (0xc,A6)  tty_$i_buf_insert(ch, &input_read)
 *   0x00E1B8CA  btst.b #0,(0x17,A2)                  input_flags bit 0 (echo)
 *   0x00E1B8D2    subq/move.b (0xc,A6)/pea (A2)      TTY_$I_XMIT_CHAR(tty, ch)
 *   0x00E1B8E0  move.w (0x2ce,A2),(0x2ca,A2)         input_head = input_tail
 *   0x00E1B8E6  move.w (0x58,A2),(0x56,A2)           saved_input_flags = column
 *   0x00E1B8EC  move.l (0x2a4,A2) / bsr              TTY_$I_ADVANCE_EC(input_ec)
 *   0x00E1B8F6  btst.b #4,(0x9,A2)                   state_flags bit 4 (SIG_PEND)
 *   0x00E1B8FE    move.w #0x1a / pea (A2)            TTY_$I_SIGNAL(tty, 0x1A)
 *   0x00E1B90C  btst.b #1,(0x9,A2)                   state_flags bit 1 (INPUT_WAIT)
 *   0x00E1B914    bclr.b #1,(0x9,A2)                 clear it
 *   0x00E1B91A    move.l (0x2a8,A2) / bsr            TTY_$I_ADVANCE_EC(output_ec)
 *
 * Byte (0x17,A2) is the low byte of the 32-bit input_flags at 0x14 and byte
 * (0x9,A2) the low byte of the 16-bit state_flags at 0x08, so the bit numbers
 * are the same in the full-width fields.
 *
 * Parameters:
 *   tty - TTY descriptor
 *   ch  - The break/newline character (byte in the high half of the word slot)
 *
 * Original address: 0x00e1b8b0
 * Size: 122 bytes
 */

#include "tty/tty_internal.h"

void TTY_$I_BREAK_CHAR(tty_desc_t *tty, uint8_t ch)
{
    /* 0x00E1B8BA: insert the break character into the input buffer */
    tty_$i_buf_insert(ch, &tty->input_read);

    /* 0x00E1B8CA: echo the character if input echo (input_flags bit 0) is on */
    if ((tty->input_flags & 0x00000001) != 0) {
        TTY_$I_XMIT_CHAR(tty, ch);
    }

    /* 0x00E1B8E0: commit the line -- head catches up with tail */
    tty->input_head = tty->input_tail;

    /* 0x00E1B8E6: latch the current column */
    tty->saved_input_flags = tty->column;

    /* 0x00E1B8EC: wake readers */
    TTY_$I_ADVANCE_EC(tty->input_ec);

    /* 0x00E1B8F6: signal pending on input -> TTY_$I_SIGNAL(tty, 0x1A) */
    if ((tty->state_flags & TTY_STATUS_SIG_PEND) != 0) {
        TTY_$I_SIGNAL(tty, TTY_SIG_WINCH);
    }

    /* 0x00E1B90C: a writer waiting on input -> clear the flag, wake it */
    if ((tty->state_flags & TTY_STATUS_INPUT_WAIT) != 0) {
        tty->state_flags &= (uint16_t)~TTY_STATUS_INPUT_WAIT;
        TTY_$I_ADVANCE_EC(tty->output_ec);
    }
}

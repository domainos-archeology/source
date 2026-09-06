// TTY_$I_FLUSH_INPUT - Flush the input buffer
// Address: 0x00e1b7b0
// Size: 86 bytes
//
// TTY_$I_FLUSH_OUTPUT - Flush the output buffer
// Address: 0x00e1b806
// Size: 30 bytes
//
// TTY_$I_OUTPUT_BUFFER_DRAINED - Called when output buffer is empty
// Address: 0x00e1b394
// Size: 32 bytes

#include "tty/tty_internal.h"

void TTY_$I_OUTPUT_BUFFER_DRAINED(tty_desc_t *tty)
{
    // Clear output wait flag
    tty->state_flags &= ~TTY_STATUS_OUTPUT_WAIT;

    // Signal that output is complete via eventcount
    TTY_$I_ADVANCE_EC(tty->output_ec);
}

void TTY_$I_FLUSH_INPUT(tty_desc_t *tty)
{
    // Reset input buffer pointers - tail = read position, head = tail
    tty->input_tail = tty->input_read;
    tty->input_head = tty->input_tail;

    // Save current column position
    tty->saved_input_flags = tty->column;

    // If waiting for input, signal completion
    if ((tty->state_flags & TTY_STATUS_INPUT_WAIT) != 0) {
        tty->state_flags &= ~TTY_STATUS_INPUT_WAIT;
        TTY_$I_ADVANCE_EC(tty->output_ec);
    }

    // Call flow control handler if set
    if (tty->flow_ctrl_handler != 0) {
        boolean xon_xoff = (tty->input_flags & 0x02) != 0 ? true : false;
        // 0xE1B7EA: sne on btst.b #1,(0x17,A2), clr.w for the 2nd argument and
        // move.l (A2) for the full 32-bit line_id.
        tty->flow_ctrl_handler(tty->line_id, false, xon_xoff);
    }
}

void TTY_$I_FLUSH_OUTPUT(tty_desc_t *tty)
{
    // Reset output buffer pointers
    tty->output_read = tty->output_head;

    // Signal that output buffer is drained
    TTY_$I_OUTPUT_BUFFER_DRAINED(tty);
}

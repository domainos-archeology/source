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

#include "tty.h"

// External helper functions (internal to TTY subsystem)
extern void FUN_00e1aef8(m68k_ptr_t ec);  // Advance eventcount

void TTY_$I_OUTPUT_BUFFER_DRAINED(tty_desc_t *tty)
{
    // Clear output wait flag
    tty->status_flags &= ~TTY_STATUS_OUTPUT_WAIT;

    // Signal that output is complete via eventcount
    FUN_00e1aef8(tty->output_ec);
}

void TTY_$I_FLUSH_INPUT(tty_desc_t *tty)
{
    // Reset input buffer pointers - tail = read position, head = tail
    tty->input_tail = tty->input_read;
    tty->input_head = tty->input_tail;

    // Save current input flags
    tty->saved_input_flags = tty->current_input_flags;

    // If waiting for input, signal completion
    if ((tty->status_flags & TTY_STATUS_INPUT_WAIT) != 0) {
        tty->status_flags &= ~TTY_STATUS_INPUT_WAIT;
        FUN_00e1aef8(tty->output_ec);
    }

    // Call flow control handler if set
    if (tty->flow_ctrl_handler != 0) {
        boolean xon_xoff = (tty->output_flags & 0x02) != 0 ? true : false;
        // Call handler: handler(line_id, false, xon_xoff)
        // This tells the handler that flow control is being cleared
        void (*handler)(short, short, char) = (void (*)(short, short, char))(uintptr_t)tty->flow_ctrl_handler;
        handler((short)tty->line_id, 0, xon_xoff);
    }
}

void TTY_$I_FLUSH_OUTPUT(tty_desc_t *tty)
{
    // Reset output buffer pointers
    tty->output_read = tty->output_head;

    // Signal that output buffer is drained
    TTY_$I_OUTPUT_BUFFER_DRAINED(tty);
}

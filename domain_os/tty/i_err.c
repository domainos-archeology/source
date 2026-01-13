// TTY_$I_ERR - Handle TTY error condition
// Address: 0x00e1be08
// Size: 160 bytes

#include "tty.h"

// External helper functions
extern void FUN_00e1aef8(m68k_ptr_t ec);   // Advance eventcount
extern void FUN_00e1bcfc(void);             // Error handling helper

// Error status codes
#define ERR_FRAMING    0x360004
#define ERR_OVERFLOW   0x360005
#define ERR_BREAK      0x36000b

void TTY_$I_ERR(tty_desc_t *tty, char fatal)
{
    status_$t err_status;

    // Call the error handler to get error status
    status_$t (*handler)(short, char) = (status_$t (*)(short, char))(uintptr_t)tty->status_handler;
    err_status = handler((short)tty->line_id, true);

    // Check error type and mode flags to determine action
    if (fatal == 0 && err_status == ERR_FRAMING) {
        // Non-fatal framing error
        if ((tty->input_flags & 0x400) == 0) {
            // Not ignoring framing errors - set error flag and signal
            goto signal_error;
        }
        // Fall through to handle via FUN_00e1bcfc
    } else {
        // Check if this is a break condition we care about
        char is_break_we_ignore = (((tty->input_flags & 0x400) != 0) &&
                                   (err_status == ERR_BREAK)) ? true : false;

        if (is_break_we_ignore < 0) {
            // Break condition but we're ignoring it
            goto handle_error;
        }

        // Check if we should signal on parity errors
        if ((tty->input_flags & 0x800) == 0) {
            goto signal_error;
        }

        // Only continue if not framing or overflow
        if (err_status != ERR_FRAMING && err_status != ERR_OVERFLOW) {
            goto signal_error;
        }
    }

handle_error:
    // Handle via error helper
    FUN_00e1bcfc();
    return;

signal_error:
    // Set error callback flag
    tty->status_flags |= TTY_ERR_CALLBACK;

    // Signal both input and output eventcounts
    FUN_00e1aef8(tty->input_ec);
    FUN_00e1aef8(tty->output_ec);
}

// TTY_$K_RESET - Reset TTY to default settings
// Address: 0x00e672de
// Size: 204 bytes

#include "tty.h"

// External lock/unlock helper functions
extern void FUN_00e1aed0(tty_desc_t *tty);  // Lock TTY
extern void FUN_00e1aee4(tty_desc_t *tty);  // Unlock TTY

void TTY_$K_RESET(short *line_ptr, status_$t *status)
{
    tty_desc_t *tty;
    short i;

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    // Lock the TTY
    FUN_00e1aed0(tty);

    // Save XON/XOFF mode flag
    boolean xon_xoff = (tty->output_flags & 0x02) != 0 ? true : false;

    // Reset input buffer pointers
    tty->input_head = 1;
    tty->input_read = 1;
    tty->input_tail = 1;
    tty->reserved_2C8 = 0x100;

    // Reset output buffer pointers
    tty->output_head = 1;
    tty->output_read = 1;
    tty->output_tail = 0x100;

    // Clear saved input flags and signal pending
    tty->saved_input_flags = 0;
    tty->current_input_flags = 0;
    tty->pending_signal = 0;

    // Reset process group UID to nil
    tty->pgroup_uid.high = UID_$NIL.high;
    tty->pgroup_uid.low = UID_$NIL.low;

    // Clear session ID
    tty->session_id = 0;

    // Clear state flags
    tty->state_flags = 0;

    // Clear delay settings
    for (i = 0; i < 5; i++) {
        tty->delay[i] = 0;
    }

    // Call XON/XOFF handler if set
    if (tty->xon_xoff_handler != 0) {
        void (*handler)(short, short, char) = (void (*)(short, short, char))(uintptr_t)tty->xon_xoff_handler;
        handler((short)tty->line_id, 0, false);
    }

    // Call flow control handler if set
    if (tty->flow_ctrl_handler != 0) {
        void (*handler)(short, short, char) = (void (*)(short, short, char))(uintptr_t)tty->flow_ctrl_handler;
        handler((short)tty->line_id, 0, xon_xoff);
    }

    // Unlock the TTY
    FUN_00e1aee4(tty);
}

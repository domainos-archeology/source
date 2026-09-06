// TTY_$K_RESET - Reset TTY to default settings
// Address: 0x00e672de
// Size: 204 bytes

#include "tty/tty_internal.h"

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
    TTY_$I_LOCK(tty);

    // Save XON/XOFF mode flag
    // Assembly: btst.b #0x1,(0x17,A2) - tests bit 1 of LSB of input_flags
    boolean xon_xoff = (tty->input_flags & 0x02) != 0 ? true : false;

    // Reset input buffer pointers
    tty->input_head = 1;
    tty->input_read = 1;
    tty->input_tail = 1;
    tty->input_size = 0x100;

    // Reset output buffer pointers
    tty->output_head = 1;
    tty->output_read = 1;
    tty->output_tail = 0x100;

    // Clear saved column and current column position
    tty->saved_input_flags = 0;
    tty->column = 0;
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
        // 0xE67374: clr.w (false) + move.l (A2) (full 32-bit line_id)
        tty->xon_xoff_handler(tty->line_id, false);
    }

    // Call flow control handler if set
    if (tty->flow_ctrl_handler != 0) {
        // 0xE67388: move.b (-0x8,A6) + clr.w + move.l (A2)
        tty->flow_ctrl_handler(tty->line_id, false, xon_xoff);
    }

    // Unlock the TTY
    TTY_$I_UNLOCK(tty);
}

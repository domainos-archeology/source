/*
 * TTY_$K_RESET - Reset a line's buffers and ownership
 * 0x00E672DE..0x00E673A8 (204 bytes), A5 = 0x00E8242C; re-verified 2026-09-22.
 *   0x00E672F0  tty = TTY_$I_GET_DESC(*line_ptr, status); status != 0 -> return
 *   0x00E6730C  TTY_$I_LOCK(tty)
 *   0x00E67316  (-0x8,A6) = sne(input_flags bit 1)  (btst.b #1,(0x17,A2))
 *   0x00E67322  move.l #0x10001,(0x2ca): input_head = input_read = 1
 *   0x00E6732A  move.l #0x10100,(0x2ce): input_tail = 1, input_size = 0x100
 *   0x00E67332  move.l #0x10001,(0x3d2): output_head = output_read = 1
 *   0x00E6733A  output_tail = 0x100
 *   0x00E67340  clr.l (0x56): saved_input_flags = column = 0; clr.w (0xa)
 *   0x00E67348  pgroup_uid = UID_$NIL (0x00E1737C); session_id = 0;
 *               state_flags = 0; dbf #4: delay[0..4] = 0
 *   0x00E6736E  xon_xoff_handler -> (line_id, false)
 *   0x00E67382  flow_ctrl_handler -> (line_id, false, the saved sne byte)
 *   0x00E67398  TTY_$I_UNLOCK(tty)
 */

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

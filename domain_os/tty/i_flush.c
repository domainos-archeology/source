/*
 * TTY_$I_OUTPUT_BUFFER_DRAINED, TTY_$I_FLUSH_INPUT, TTY_$I_FLUSH_OUTPUT
 *
 * Three small entry points in the TTY module (map: "I E1AED0 TTY").
 *
 * TTY_$I_OUTPUT_BUFFER_DRAINED, 0x00E1B394..0x00E1B3B2 (32 bytes):
 *   bclr.b #0,(0x9,A2)             state_flags bit 0 (OUTPUT_WAIT) cleared
 *   move.l (0x2a8,A2) / bsr        TTY_$I_ADVANCE_EC(output_ec)
 *
 * TTY_$I_FLUSH_INPUT, 0x00E1B7B0..0x00E1B804 (86 bytes):
 *   0x00E1B7BA  input_tail = input_read; input_head = input_tail
 *   0x00E1B7C6  saved_input_flags = column
 *   0x00E1B7CC  state_flags bit 1 (INPUT_WAIT) -> clear, ADVANCE_EC(output_ec)
 *   0x00E1B7E4  flow_ctrl_handler != 0 ->
 *               flow_ctrl_handler(line_id, false (clr.w), input_flags bit 1
 *               (sne of btst.b #1,(0x17,A2)))
 *
 * TTY_$I_FLUSH_OUTPUT, 0x00E1B806..0x00E1B822 (30 bytes):
 *   output_read = output_head; TTY_$I_OUTPUT_BUFFER_DRAINED(tty)
 */

#include "tty/tty_internal.h"

void TTY_$I_OUTPUT_BUFFER_DRAINED(tty_desc_t *tty)
{
    tty->state_flags &= (uint16_t)~TTY_STATUS_OUTPUT_WAIT;   /* 0x00E1B39E */
    TTY_$I_ADVANCE_EC(tty->output_ec);                       /* 0x00E1B3A4 */
}

void TTY_$I_FLUSH_INPUT(tty_desc_t *tty)
{
    boolean hw_flow;

    tty->input_tail = tty->input_read;                       /* 0x00E1B7BA */
    tty->input_head = tty->input_tail;                       /* 0x00E1B7C0 */
    tty->saved_input_flags = tty->column;                    /* 0x00E1B7C6 */

    if ((tty->state_flags & TTY_STATUS_INPUT_WAIT) != 0) {   /* 0x00E1B7CC */
        tty->state_flags &= (uint16_t)~TTY_STATUS_INPUT_WAIT;
        TTY_$I_ADVANCE_EC(tty->output_ec);
    }

    if (tty->flow_ctrl_handler != 0) {                       /* 0x00E1B7E4 */
        hw_flow = ((tty->input_flags & 0x00000002) != 0) ? true : false;
        tty->flow_ctrl_handler(tty->line_id, false, hw_flow);
    }
}

void TTY_$I_FLUSH_OUTPUT(tty_desc_t *tty)
{
    tty->output_read = tty->output_head;                     /* 0x00E1B810 */
    TTY_$I_OUTPUT_BUFFER_DRAINED(tty);                       /* 0x00E1B818 */
}

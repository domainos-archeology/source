// TTY kernel-level flag manipulation functions
//
// TTY_$K_SET_FLAG - Set a TTY flag
// Address: 0x00e67422
//
// TTY_$K_INQ_FLAGS - Inquire TTY flags
// Address: 0x00e6748c
//
// TTY_$K_SET_INPUT_FLAG - Set input processing flag
// Address: 0x00e67656
//
// TTY_$K_INQ_INPUT_FLAGS - Inquire input processing flags
// Address: 0x00e676b0
//
// TTY_$K_SET_OUTPUT_FLAG - Set output processing flag
// Address: 0x00e676ee
//
// TTY_$K_INQ_OUTPUT_FLAGS - Inquire output processing flags
// Address: 0x00e67748
//
// TTY_$K_SET_ECHO_FLAG - Set echo flag
// Address: 0x00e67786
//
// TTY_$K_INQ_ECHO_FLAGS - Inquire echo flags
// Address: 0x00e677e0

/*
 * Re-verified 2026-09-22 against 0x00E67422..0x00E6781C.  Every routine
 * loads A5 = 0x00E8242C and begins tty = TTY_$I_GET_DESC(*line_ptr, status);
 * status != 0 -> return.
 *   TTY_$K_SET_FLAG (0x00E67422, 106 bytes): *flag_ptr (word) != 0 -> return
 *     with status untouched; *value_ptr < 0 -> bset.b #4,(0x9,A2) and
 *     input_read != input_head -> TTY_$I_SIGNAL(tty, 0x1A); else bclr.b #4
 *   TTY_$K_INQ_FLAGS (0x00E6748C, 70 bytes): clr.w (A1); state bit 4 ->
 *     bset.b #0,(0x1,A1) = bit 0 of the caller's word
 *   TTY_$K_SET_INPUT_FLAG / _OUTPUT_FLAG / _ECHO_FLAG (0x00E67656 /
 *     0x00E676EE / 0x00E67786, 90 bytes each): bset.l D2,D1 on a cleared
 *     longword = 1 << (*flag_ptr mod 32); *value_ptr < 0 -> or.l into
 *     input_flags (0x14) / output_flags (0xc) / echo_flags (0x1c), else
 *     not.l / and.l
 *   TTY_$K_INQ_INPUT_FLAGS / _OUTPUT_FLAGS / _ECHO_FLAGS (0x00E676B0 /
 *     0x00E67748 / 0x00E677E0, 62 bytes each): copy the longword out
 */
#include "tty/tty_internal.h"

void TTY_$K_SET_FLAG(short *line_ptr, short *flag_ptr, char *value_ptr,
                     status_$t *status)
{
    tty_desc_t *tty;

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    // Currently only flag 0 (signal on input available) is supported
    if (*flag_ptr != 0) {
        return;
    }

    if (*value_ptr < 0) {  // true in Domain/OS convention
        // Enable signal on input available
        tty->state_flags |= TTY_STATUS_SIG_PEND;

        // If there's already input, signal immediately
        if (tty->input_read != tty->input_head) {
            TTY_$I_SIGNAL(tty, TTY_SIG_WINCH);
        }
    } else {
        // Disable signal on input available
        tty->state_flags &= ~TTY_STATUS_SIG_PEND;
    }
}

void TTY_$K_INQ_FLAGS(short *line_ptr, uint16_t *flags_ptr, status_$t *status)
{
    tty_desc_t *tty;

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    // Clear output flags
    *flags_ptr = 0;

    // Check if signal on input available is enabled
    if ((tty->state_flags & TTY_STATUS_SIG_PEND) != 0) {
        *flags_ptr |= 0x0001;
    }
}

void TTY_$K_SET_INPUT_FLAG(short *line_ptr, const uint16_t *flag_ptr,
                           const char *value_ptr,
                           status_$t *status)
{
    tty_desc_t *tty;

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    // Set or clear the specified bit in input_flags
    if (*value_ptr < 0) {  // true
        tty->input_flags |= (1U << (*flag_ptr & 0x1F));
    } else {
        tty->input_flags &= ~(1U << (*flag_ptr & 0x1F));
    }
}

void TTY_$K_INQ_INPUT_FLAGS(short *line_ptr, uint32_t *flags_ptr, status_$t *status)
{
    tty_desc_t *tty;

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    *flags_ptr = tty->input_flags;
}

void TTY_$K_SET_OUTPUT_FLAG(short *line_ptr, const uint16_t *flag_ptr,
                            const char *value_ptr,
                            status_$t *status)
{
    tty_desc_t *tty;

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    // Set or clear the specified bit in output control flags (at offset 0x0C)
    if (*value_ptr < 0) {  // true
        tty->output_flags |= (1U << (*flag_ptr & 0x1F));
    } else {
        tty->output_flags &= ~(1U << (*flag_ptr & 0x1F));
    }
}

void TTY_$K_INQ_OUTPUT_FLAGS(short *line_ptr, uint32_t *flags_ptr, status_$t *status)
{
    tty_desc_t *tty;

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    *flags_ptr = tty->output_flags;
}

void TTY_$K_SET_ECHO_FLAG(short *line_ptr, ushort *flag_ptr, char *value_ptr,
                          status_$t *status)
{
    tty_desc_t *tty;

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    // Set or clear the specified bit in echo_flags
    if (*value_ptr < 0) {  // true
        tty->echo_flags |= (1U << (*flag_ptr & 0x1F));
    } else {
        tty->echo_flags &= ~(1U << (*flag_ptr & 0x1F));
    }
}

void TTY_$K_INQ_ECHO_FLAGS(short *line_ptr, uint32_t *flags_ptr, status_$t *status)
{
    tty_desc_t *tty;

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    *flags_ptr = tty->echo_flags;
}

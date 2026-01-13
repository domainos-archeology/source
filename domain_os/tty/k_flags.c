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

#include "tty.h"

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
        tty->status_flags |= TTY_STATUS_SIG_PEND;

        // If there's already input, signal immediately
        if (tty->input_read != tty->input_head) {
            TTY_$I_SIGNAL(tty, TTY_SIG_WINCH);
        }
    } else {
        // Disable signal on input available
        tty->status_flags &= ~TTY_STATUS_SIG_PEND;
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
    if ((tty->status_flags & TTY_STATUS_SIG_PEND) != 0) {
        *flags_ptr |= 0x0001;
    }
}

void TTY_$K_SET_INPUT_FLAG(short *line_ptr, ushort *flag_ptr, char *value_ptr,
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

void TTY_$K_SET_OUTPUT_FLAG(short *line_ptr, ushort *flag_ptr, char *value_ptr,
                            status_$t *status)
{
    tty_desc_t *tty;

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    // Set or clear the specified bit in output control flags (at offset 0x0C)
    uint32_t *output_flags_ptr = (uint32_t *)&tty->output_flags;
    if (*value_ptr < 0) {  // true
        *output_flags_ptr |= (1U << (*flag_ptr & 0x1F));
    } else {
        *output_flags_ptr &= ~(1U << (*flag_ptr & 0x1F));
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

    *flags_ptr = *(uint32_t *)&tty->output_flags;
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

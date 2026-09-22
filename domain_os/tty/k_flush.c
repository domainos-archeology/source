/*
 * TTY_$K_FLUSH_INPUT (0x00E1C084..0x00E1C0E4) and TTY_$K_FLUSH_OUTPUT
 * (0x00E1C0E6..0x00E1C146), 98 bytes each, A5 = 0x00E2DDB4.  Re-verified
 * 2026-09-22: tty = TTY_$I_GET_DESC(*line_ptr, status); status != 0 ->
 * return; token = ML_$SPIN_LOCK(&TTY_$SPIN_LOCK) into (-0x6,A6);
 * TTY_$I_FLUSH_INPUT / _OUTPUT(tty); ML_$SPIN_UNLOCK(&TTY_$SPIN_LOCK, token).
 */

#include "tty/tty_internal.h"

/* ML_$SPIN_LOCK, ML_$SPIN_UNLOCK declared in ml/ml.h via tty.h */

void TTY_$K_FLUSH_INPUT(short *line_ptr, status_$t *status)
{
    tty_desc_t *tty;
    ml_$spin_token_t token;

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    // Acquire spin lock for TTY operations
    token = ML_$SPIN_LOCK(&TTY_$SPIN_LOCK);

    // Flush the input buffer
    TTY_$I_FLUSH_INPUT(tty);

    // Release spin lock
    ML_$SPIN_UNLOCK(&TTY_$SPIN_LOCK, token);
}

void TTY_$K_FLUSH_OUTPUT(short *line_ptr, status_$t *status)
{
    tty_desc_t *tty;
    ml_$spin_token_t token;

    // Get TTY descriptor for this line
    tty = TTY_$I_GET_DESC(*line_ptr, status);
    if (*status != status_$ok) {
        return;
    }

    // Acquire spin lock for TTY operations
    token = ML_$SPIN_LOCK(&TTY_$SPIN_LOCK);

    // Flush the output buffer
    TTY_$I_FLUSH_OUTPUT(tty);

    // Release spin lock
    ML_$SPIN_UNLOCK(&TTY_$SPIN_LOCK, token);
}

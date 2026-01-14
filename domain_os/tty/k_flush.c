// TTY_$K_FLUSH_INPUT - Flush input buffer (kernel level)
// Address: 0x00e1c084
// Size: 98 bytes
//
// TTY_$K_FLUSH_OUTPUT - Flush output buffer (kernel level)
// Address: 0x00e1c0e6
// Size: 98 bytes

#include "tty.h"

/* ML_$SPIN_LOCK, ML_$SPIN_UNLOCK declared in ml/ml.h via tty.h */

void TTY_$K_FLUSH_INPUT(short *line_ptr, status_$t *status)
{
    tty_desc_t *tty;
    ushort token;

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
    ushort token;

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

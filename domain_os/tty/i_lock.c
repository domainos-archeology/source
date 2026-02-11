/*
 * TTY_$I_LOCK - Acquire TTY lock
 *
 * Acquires the TTY subsystem lock (lock ID 3).
 * This is called before TTY operations to ensure exclusive access.
 *
 * Parameters:
 *   tty - TTY descriptor (unused - lock is global)
 *
 * Original address: 0x00e1aed0
 * Size: 20 bytes
 */

#include "tty/tty_internal.h"

void TTY_$I_LOCK(tty_desc_t *tty)
{
    (void)tty;  /* Parameter is unused - TTY lock is global */
    ML_$LOCK(TTY_LOCK_ID);
}

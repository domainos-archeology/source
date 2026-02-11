/*
 * TTY_$I_UNLOCK - Release TTY lock
 *
 * Releases the TTY subsystem lock (lock ID 3).
 * This is called after TTY operations to allow other
 * processes to access the TTY.
 *
 * Parameters:
 *   tty - TTY descriptor (unused - lock is global)
 *
 * Original address: 0x00e1aee4
 * Size: 20 bytes
 */

#include "tty/tty_internal.h"

void TTY_$I_UNLOCK(tty_desc_t *tty)
{
    (void)tty;  /* Parameter is unused - TTY lock is global */
    ML_$UNLOCK(TTY_LOCK_ID);
}

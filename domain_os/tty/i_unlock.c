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
 * 0x00E1AEE4..0x00E1AEF6: link.w A6,-0x4; subq.l #0x2,SP; move.w #0x3,-(SP);
 * jsr ML_$UNLOCK (0x00E20B62).  The callers push a tty pointer (e.g. TTY_$K_RESET
 * 0x00E6730C pea (A2)) but the body never reads (0x8,A6); the subq.l #2 only
 * pads the word argument to a longword slot.
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

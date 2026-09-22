/*
 * KBD_$CRASH_INIT - Prepare the keyboard for the crash console
 *
 * An empty procedure: the image holds a single `rts`.  Called by KBD_$RCV
 * after it has crashed the system on the manual-stop key (0x00E1CD2E), and
 * from the crash console setup.
 *
 * Original address: 0x00e1ce94, 2 bytes
 *
 *   00e1ce94  rts
 */

#include "kbd/kbd_internal.h"

void KBD_$CRASH_INIT(void)
{
    /* 0x00E1CE94: rts */
}

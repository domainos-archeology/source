/*
 * KBD_$RESET - Reset the keyboard
 *
 * An empty procedure: the image holds a single `rts` (map: KBD_$RESET at
 * 0x00E1AB28, the last symbol before the OS_TERM_INIT segment at 0x00E1AB2A).
 *
 * Original address: 0x00e1ab28, 2 bytes
 *
 *   00e1ab28  rts
 */

#include "kbd/kbd_internal.h"

void KBD_$RESET(void)
{
    /* 0x00E1AB28: rts */
}

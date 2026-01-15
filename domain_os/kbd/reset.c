/*
 * KBD_$RESET - Reset keyboard controller
 *
 * Stub function - keyboard reset is a no-op in this implementation.
 *
 * Original address: 0x00e1ab28
 */

#include "kbd/kbd_internal.h"

void KBD_$RESET(void)
{
    /* No-op - keyboard doesn't require explicit reset */
    return;
}

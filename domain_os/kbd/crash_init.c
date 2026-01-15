/*
 * KBD_$CRASH_INIT - Initialize keyboard for crash console
 *
 * Stub function - crash console keyboard initialization is a no-op.
 *
 * Original address: 0x00e1ce94
 */

#include "kbd/kbd_internal.h"

void KBD_$CRASH_INIT(void)
{
    /* No-op - crash console doesn't need special keyboard init */
    return;
}

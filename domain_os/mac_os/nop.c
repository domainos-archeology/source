/*
 * MAC_OS_$NOP - No operation placeholder
 *
 * An empty function used as a placeholder during initialization.
 * This is called during MAC_OS_$INIT but does nothing.
 *
 * Original address: 0x00E0B244
 * Original size: 2 bytes (just RTS)
 */

#include "mac_os/mac_os_internal.h"

/*
 * MAC_OS_$NOP
 *
 * This is a placeholder function that simply returns.
 * It exists in the original code presumably as a hook point
 * that could be patched or for alignment purposes.
 *
 * The original assembly is just:
 *   rts
 */
void MAC_OS_$NOP(void)
{
    /* Do nothing - just return */
}

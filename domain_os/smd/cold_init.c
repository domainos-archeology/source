/*
 * smd/cold_init.c - SMD_$COLD_INIT implementation
 *
 * Cold initialization for the SMD subsystem.
 * In the original, this is a no-op stub (just rts).
 *
 * Original address: 0x00E34F00
 *
 * Assembly:
 *   00e34f00    rts
 */

#include "smd/smd_internal.h"

/*
 * SMD_$COLD_INIT - Cold initialization
 *
 * This function is called during cold boot but performs no operation.
 * All actual initialization is done in SMD_$INIT.
 */
void SMD_$COLD_INIT(void)
{
    /* No operation - just returns */
    return;
}

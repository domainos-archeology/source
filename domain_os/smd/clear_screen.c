/*
 * smd/clear_screen.c - SMD_$CLEAR_SCREEN implementation
 *
 * This is a stub function that does nothing. Screen clearing
 * is handled elsewhere or not implemented in this version.
 *
 * Original address: 0x00E70022
 */

#include "smd/smd_internal.h"

/*
 * SMD_$CLEAR_SCREEN - Clear screen (stub)
 *
 * This function is a no-op stub. In the original binary it consists
 * of just an RTS instruction.
 *
 * Parameters:
 *   status_ret - Status return (not used)
 *
 * Original address: 0x00E70022
 *
 * Assembly:
 *   00e70022    rts
 */
void SMD_$CLEAR_SCREEN(status_$t *status_ret)
{
    (void)status_ret;   /* Unused */
    /* No-op - original is just RTS */
}

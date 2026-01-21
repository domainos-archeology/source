/*
 * smd/busy_wait.c - SMD_$BUSY_WAIT implementation
 *
 * Busy wait function (stub - always succeeds).
 *
 * Original address: 0x00E1D89E
 *
 * Assembly:
 *   00e1d89e    link.w A6,0x0
 *   00e1d8a2    pea (A5)
 *   00e1d8a4    lea (0xe2e060).l,A5      ; Load SMD base
 *   00e1d8aa    movea.l (0xc,A6),A0      ; Get status_ret pointer
 *   00e1d8ae    clr.l (A0)               ; Set status to 0 (success)
 *   00e1d8b0    movea.l (-0x4,A6),A5
 *   00e1d8b4    unlk A6
 *   00e1d8b6    rts
 *
 * This function appears to be a placeholder that always returns success.
 * The first parameter is unused.
 */

#include "smd/smd_internal.h"

/*
 * SMD_$BUSY_WAIT - Wait function (stub)
 *
 * This function appears to be a placeholder or stub that performs no
 * actual waiting. It simply returns success immediately.
 *
 * Parameters:
 *   param1     - Unused parameter (possibly a timeout or wait value)
 *   status_ret - Status return pointer
 *
 * Returns:
 *   status_$ok always
 */
void SMD_$BUSY_WAIT(uint32_t param1, status_$t *status_ret)
{
    /* Unused parameter */
    (void)param1;

    *status_ret = status_$ok;
}

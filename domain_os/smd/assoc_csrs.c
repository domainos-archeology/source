/*
 * smd/assoc_csrs.c - SMD_$ASSOC_CSRS implementation
 *
 * Associate display with cursors (stub).
 *
 * Original address: 0x00E700E8
 *
 * This function is a stub that returns an unsupported status code.
 * Cursor association with displays may not have been implemented in this
 * version of Domain/OS, or was handled through different mechanisms.
 *
 * Assembly:
 *   00e700e8    link.w A6,0x0
 *   00e700ec    pea (A5)
 *   00e700ee    lea (0xe82b8c).l,A5
 *   00e700f4    movea.l (0x10,A6),A0          ; A0 = status_ret (3rd param)
 *   00e700f8    move.l #0x130028,(A0)         ; *status_ret = unsupported
 *   00e700fe    movea.l (-0x4,A6),A5
 *   00e70102    unlk A6
 *   00e70104    rts
 */

#include "smd/smd_internal.h"

/*
 * SMD_$ASSOC_CSRS - Associate display with cursors
 *
 * This is a stub function that always returns an unsupported status.
 * The operation was not implemented in this version of the system.
 *
 * Parameters:
 *   param_1    - Unused (likely intended for unit number)
 *   param_2    - Unused (likely intended for cursor bitmap or config)
 *   status_ret - Status return (always set to unsupported)
 */
void SMD_$ASSOC_CSRS(uint32_t param_1, uint32_t param_2, status_$t *status_ret)
{
    (void)param_1;  /* Unused */
    (void)param_2;  /* Unused */

    *status_ret = status_$display_nonconforming_blts_unsupported;
}

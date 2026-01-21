/*
 * smd/dissoc.c - SMD_$DISSOC implementation
 *
 * Dissociate display from process (stub).
 *
 * Original address: 0x00E70106
 *
 * This function is a stub that returns an unsupported status code.
 * Display dissociation may not have been fully implemented in this
 * version of Domain/OS, or the functionality was handled differently.
 *
 * Assembly:
 *   00e70106    link.w A6,0x0
 *   00e7010a    pea (A5)
 *   00e7010c    lea (0xe82b8c).l,A5
 *   00e70112    movea.l (0xc,A6),A0           ; A0 = status_ret
 *   00e70116    move.l #0x130028,(A0)         ; *status_ret = unsupported
 *   00e7011c    movea.l (-0x4,A6),A5
 *   00e70120    unlk A6
 *   00e70122    rts
 */

#include "smd/smd_internal.h"

/*
 * SMD_$DISSOC - Dissociate display from process
 *
 * This is a stub function that always returns an unsupported status.
 * The operation was not implemented in this version of the system.
 *
 * Parameters:
 *   param_1    - Unused (likely intended for unit number)
 *   status_ret - Status return (always set to unsupported)
 */
void SMD_$DISSOC(uint32_t param_1, status_$t *status_ret)
{
    (void)param_1;  /* Unused */

    *status_ret = status_$display_nonconforming_blts_unsupported;
}

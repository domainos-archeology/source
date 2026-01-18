/*
 * PACCT_$INIT - Initialize the process accounting subsystem
 *
 * Sets the accounting owner to UID_$NIL (disabling accounting) and
 * clears all state variables.
 *
 * Original address: 0x00E31CE8
 * Size: 34 bytes
 *
 * Assembly:
 *   link.w A6,0x0
 *   movea.l #0xe1737c,A0          ; A0 = &UID_$NIL
 *   movea.l #0xe817ec,A1          ; A1 = &pacct_owner
 *   move.l (A0)+,(A1)             ; pacct_owner.high = UID_$NIL.high
 *   move.l (A0)+,(0x4,A1)         ; pacct_owner.low = UID_$NIL.low
 *   clr.l (0x18,A1)               ; map_ptr = 0
 *   clr.l (0xc,A1)                ; buf_remaining = 0
 *   unlk A6
 *   rts
 */

#include "pacct_internal.h"

/* Global accounting state - located at 0xE817EC on m68k */
pacct_state_t pacct_state;

void PACCT_$INIT(void)
{
    /* Disable accounting by setting owner to nil */
    pacct_owner.high = UID_$NIL.high;
    pacct_owner.low = UID_$NIL.low;

    /* Clear state variables */
    DAT_00e81804 = NULL;    /* map_ptr = 0 */
    DAT_00e817f8 = 0;       /* buf_remaining = 0 */
}

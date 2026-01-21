/*
 * smd/init_state.c - SMD_$INIT_STATE implementation
 *
 * Initialize display state for the current process.
 *
 * Original address: 0x00E6F818
 *
 * Assembly:
 *   link.w A6,0x0
 *   pea (A5)
 *   lea (0xe82b8c).l,A5      ; SMD_GLOBALS base
 *   subq.l #0x2,SP
 *   move.l (0x8,A6),-(SP)    ; push status_ret
 *   clr.w -(SP)              ; push 0 (options)
 *   bsr.w FUN_00e6f514       ; call smd_$init_display_state(0, status_ret)
 *   movea.l (-0x4,A6),A5
 *   unlk A6
 *   rts
 */

#include "smd/smd_internal.h"

/*
 * SMD_$INIT_STATE - Initialize display state
 *
 * Initializes the display state for the current process. This is a
 * wrapper around smd_$init_display_state with options=0.
 *
 * Parameters:
 *   status_ret - Output: status return
 */
void SMD_$INIT_STATE(status_$t *status_ret)
{
    /* Call internal function with options = 0 */
    smd_$init_display_state(0, status_ret);
}

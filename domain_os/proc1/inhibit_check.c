/*
 * PROC1_$INHIBIT_CHECK - Check if process is inhibited
 *
 * Checks if the process has a non-zero inhibit count, which
 * indicates it's in an inhibit region and should not be
 * preempted or suspended.
 *
 * Parameters:
 *   pcb - Process to check
 *
 * Returns:
 *   -1 (0xFF) if inhibited (nesting_depth != 0)
 *   0 if not inhibited (nesting_depth == 0)
 *
 * Original address: 0x00e20ef0
 */

#include "proc1/proc1_internal.h"

int8_t PROC1_$INHIBIT_CHECK(proc1_t *pcb)
{
    /*
     * Assembly (0x00e20ef0):
     *   movea.l (0x4,SP), A1         ; pcb parameter
     *   tst.w   (0x5a,A1)            ; test nesting_depth
     *   sne     D0                   ; D0 = -1 if nonzero, 0 if zero
     *   rts
     */
    return -(pcb->nesting_depth != 0);
}

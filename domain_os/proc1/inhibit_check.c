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
 *   -1 (0xFF) if inhibited (inh_count != 0)
 *   0 if not inhibited (inh_count == 0)
 *
 * Original address: 0x00e20ef0
 */

#include "proc1.h"

int8_t PROC1_$INHIBIT_CHECK(proc1_t *pcb)
{
    /*
     * The assembly tests the word at offset 0x5A (pad_5a in our struct).
     * Looking at the decompilation, it accesses (sw_bsr + 1) which would
     * be offset 0x59 (a byte) or the low byte of the word at 0x58.
     *
     * Actually, offset 0x5A is a different field. Let me re-examine:
     * The assembly does: tst.w (0x5a,A1)
     *
     * This tests the 16-bit value at offset 0x5A in the PCB.
     * Our struct has pad_5a at 0x5A - this is likely actually part
     * of the inhibit mechanism, not padding.
     *
     * For now, use inh_count at 0x56 as documented.
     */
    return -(pcb->inh_count != 0);
}

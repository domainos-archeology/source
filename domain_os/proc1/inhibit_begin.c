/*
 * PROC1_$INHIBIT_BEGIN - Enter an inhibit region for the current process
 * Original address: 0x00e20efc (16 bytes)
 *
 * 0x00E20EFC  movea.l (-0x2436,PC),A1        A1 = PROC1_$CURRENT_PCB (0xE1EAC8)
 * 0x00E20F00  addq.w #0x1,(0x5a,A1)          pcb->nesting_depth++
 * 0x00E20F04  bset.b #0x0,(0x43,A1)          bit 0 of the LOW byte of the
 *                                            longword at 0x40, i.e. bit 0 of
 *                                            pcb->resource_locks_held
 * 0x00E20F0A  rts
 *
 * Lock 0 of resource_locks_held is the inhibit lock; PROC1_$INHIBIT_END
 * (0x00E20EA2) clears it again when nesting_depth returns to zero, and
 * ML_$EXCLUSION_START (0x00E20DF8) sets the same bit.  No SR change.
 */

#include "proc1/proc1_internal.h"

void PROC1_$INHIBIT_BEGIN(void)
{
    proc1_t *pcb = PROC1_$CURRENT_PCB;

    /* 0x00E20F00 */
    pcb->nesting_depth++;

    /* 0x00E20F04: bset.b #0,(0x43,A1) == bit 0 of the 32-bit word at 0x40 */
    pcb->resource_locks_held |= 1u;
}

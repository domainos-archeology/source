/*
 * PROC1_$INHIBIT_BEGIN - Begin an inhibit region
 *
 * Increments the inhibit counter and sets a flag to prevent
 * the process from being preempted. Used to protect critical
 * sections that shouldn't be interrupted.
 *
 * Must be paired with PROC1_$INHIBIT_END.
 *
 * Original address: 0x00e20efc
 */

#include "proc1/proc1_internal.h"

/*
 * The assembly accesses offset 0x5A (nesting_depth) as the inhibit/lock
 * nesting counter and sets bit 0 of byte at offset 0x43 (lowest byte of
 * resource_locks_held on big-endian m68k).
 *
 * Assembly (0x00e20efc):
 *   movea.l PROC1_$CURRENT_PCB, A1
 *   addq.w  #1, (0x5a,A1)          ; increment nesting_depth
 *   bset.b  #0, (0x43,A1)          ; set inhibit flag in resource_locks_held LSB
 */

void PROC1_$INHIBIT_BEGIN(void)
{
    proc1_t *pcb = PROC1_$CURRENT_PCB;

    /* Increment nesting depth counter (offset 0x5A) */
    pcb->nesting_depth++;

    /*
     * Set bit 0 of the low byte of resource_locks_held.
     * On big-endian m68k, byte at offset 0x43 is the LSB.
     * This flag indicates "inhibited" state.
     *
     * We use a bitwise OR on the full word since we're on
     * a potentially little-endian host.
     */
    pcb->resource_locks_held |= 0x01;
}

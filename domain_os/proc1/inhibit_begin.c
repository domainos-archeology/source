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

#include "proc1.h"

/*
 * Note: The assembly accesses offset 0x5A as the inhibit counter
 * and sets bit 0 of byte at offset 0x43 (lowest byte of
 * resource_locks_held on big-endian m68k).
 *
 * The field at 0x5A was originally labeled pad_5a but is actually
 * the inhibit counter. We use inh_count at 0x56 in our struct
 * which may need adjustment.
 *
 * TODO: Verify PCB layout - the inhibit counter might be at 0x5A
 * not 0x56 as originally thought.
 */

void PROC1_$INHIBIT_BEGIN(void)
{
    proc1_t *pcb = PROC1_$CURRENT_PCB;

    /* Increment inhibit counter */
    pcb->inh_count++;

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
